/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <list>
#include <set>

#include "Debug.h"
#include "Exception.h"
#include "FileManager.h"
#include "IFileSystem.h"
#include "IStream.h"

namespace spades {
	static std::list<IFileSystem*> g_fileSystems;
	// What the Overlays alive on this thread put on top, the latest last.
	static thread_local std::vector<std::vector<IFileSystem*>> g_overlays;

	// Calls `f` on each file system a read on this thread sees, the topmost
	// first, until it returns true; returns whether one did.
	template <class F> static bool AnyReadable(F f) {
		for (auto it = g_overlays.rbegin(); it != g_overlays.rend(); ++it) {
			for (auto* fs : *it) {
				if (f(fs))
					return true;
			}
		}
		for (auto* fs : g_fileSystems) {
			if (f(fs))
				return true;
		}
		return false;
	}

	FileManager::Overlay::Overlay(std::vector<IFileSystem*> fileSystems) {
		g_overlays.push_back(std::move(fileSystems));
	}
	FileManager::Overlay::~Overlay() { g_overlays.pop_back(); }

	std::unique_ptr<IStream> FileManager::OpenForReading(const char* fn) {
		SPADES_MARK_FUNCTION();
		if (!fn)
			SPInvalidArgument("fn");
		if (fn[0] == 0)
			SPFileNotFound(fn);

		// check each file system
		std::unique_ptr<IStream> stream;
		auto open = [&](const char* name) {
			return AnyReadable([&](IFileSystem* fs) {
				if (!fs->FileExists(name))
					return false;
				stream = fs->OpenForReading(name);
				return true;
			});
		};
		if (open(fn))
			return stream;

		// check weak files, too
		auto weak_fn = std::string(fn) + ".weak";
		if (open(weak_fn.c_str()))
			return stream;

		SPFileNotFound(fn);
	}
	std::unique_ptr<IStream> FileManager::OpenForWriting(const char* fn) {
		SPADES_MARK_FUNCTION();
		if (!fn)
			SPInvalidArgument("fn");
		if (fn[0] == 0)
			SPFileNotFound(fn);
		for (auto* fs : g_fileSystems) {
			if (fs->FileExists(fn))
				return fs->OpenForWriting(fn);
		}

		// FIXME: handling of weak files

		// create file
		for (auto* fs : g_fileSystems) {
			try {
				return fs->OpenForWriting(fn);
			} catch (...) {}
		}

		SPRaise("No filesystem is writable");
	}
	bool FileManager::FileExists(const char* fn) {
		SPADES_MARK_FUNCTION();
		if (!fn)
			SPInvalidArgument("fn");

		if (AnyReadable([&](IFileSystem* fs) { return fs->FileExists(fn); }))
			return true;

		// check weak files, too
		auto weak_fn = std::string(fn) + ".weak";
		if (AnyReadable([&](IFileSystem* fs) { return fs->FileExists(weak_fn.c_str()); }))
			return true;

		return false;
	}

	bool FileManager::RemoveFile(const char* fn) {
		SPADES_MARK_FUNCTION();
		if (!fn)
			SPInvalidArgument("fn");
		for (auto* fs : g_fileSystems) {
			if (fs->FileExists(fn))
				return fs->RemoveFile(fn);
		}
		return false;
	}

	bool FileManager::RenameFile(const char* oldName, const char* newName) {
		SPADES_MARK_FUNCTION();
		if (!oldName || !newName)
			SPInvalidArgument("oldName/newName");
		for (auto* fs : g_fileSystems) {
			if (fs->FileExists(oldName))
				return fs->RenameFile(oldName, newName);
		}
		return false;
	}

	void FileManager::AddFileSystem(spades::IFileSystem* fs) {
		SPADES_MARK_FUNCTION();
		AppendFileSystem(fs);
	}

	void FileManager::AppendFileSystem(spades::IFileSystem* fs) {
		SPADES_MARK_FUNCTION();
		if (!fs)
			SPInvalidArgument("fs");
		g_fileSystems.push_back(fs);
	}
	void FileManager::PrependFileSystem(spades::IFileSystem* fs) {
		SPADES_MARK_FUNCTION();
		if (!fs)
			SPInvalidArgument("fs");
		g_fileSystems.push_front(fs);
	}

	std::unique_ptr<IFileSystem> FileManager::RemoveFileSystem(spades::IFileSystem* fs) {
		SPADES_MARK_FUNCTION();
		if (!fs)
			SPInvalidArgument("fs");
		for (auto it = g_fileSystems.begin(); it != g_fileSystems.end(); ++it) {
			if (*it == fs) {
				g_fileSystems.erase(it);
				return std::unique_ptr<IFileSystem>(fs);
			}
		}
		return nullptr;
	}

	std::string FileManager::ReadAllBytes(const char* fn) {
		SPADES_MARK_FUNCTION();

		auto stream = OpenForReading(fn);
		SPAssert(stream);

		std::string ret = stream->ReadAllBytes();
		return ret;
	}

	std::vector<std::string> FileManager::EnumFiles(const char* path) {
		std::vector<std::string> list;
		std::set<std::string> set;
		if (!path)
			SPInvalidArgument("path");

		AnyReadable([&](IFileSystem* fs) {
			for (const std::string& name : fs->EnumFiles(path))
				set.insert(name);
			return false;
		});

		for (auto& s : set)
			list.push_back(s);

		return list;
	}

	void FileManager::Close() {
		for (auto* fs : g_fileSystems)
			delete fs;
		g_fileSystems.clear();
	}
} // namespace spades