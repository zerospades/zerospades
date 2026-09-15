/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "KV6ScreenHelper.h"

#include <algorithm>
#include <cstdio>

#include <Core/LocalFileSystem.h>
#include <Core/StdStream.h>
#include <Core/VoxelModel.h>
#include <Gui/Main.h>

namespace spades {
	namespace gui {
		namespace fs = LocalFileSystem;

		namespace {
			// Model files the explorer lists (.kv6 is editable; .2kv6/.vxl are
			// shown but not yet supported).
			bool IsModelFile(const std::string& s) {
				return fs::HasExtension(s, ".kv6") || fs::HasExtension(s, ".2kv6") ||
				       fs::HasExtension(s, ".vxl");
			}

			std::string ToLower(const std::string& s) {
				std::string out = s;
				for (char& c : out)
					c = (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
				return out;
			}

			// Names of the entries in `absDir` matching `wantFolders`/`accept`,
			// sorted case-insensitively.
			template <class Pred>
			std::vector<std::string> ListNames(const std::string& absDir, bool wantFolders,
			                                   Pred accept) {
				std::vector<fs::DirEntry> entries;
				fs::ListDirectory(absDir, entries, false);
				std::vector<std::string> out;
				for (const fs::DirEntry& e : entries) {
					if (e.isFolder == wantFolders && accept(e.name))
						out.push_back(e.name);
				}
				std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
					return ToLower(a) < ToLower(b);
				});
				return out;
			}
		} // namespace

		KV6ScreenHelper::KV6ScreenHelper() {
			// Home is a dedicated `kv6/` folder inside the app-data dir (alongside
			// Mods/, Demos/, ...), created on demand. The parent already exists (the
			// game creates it at startup), so a single mkdir is enough. The explorer
			// can still browse freely up to the filesystem root from there.
			defaultDirAbs = fs::Join(std::string(spades::g_userResourceDirectory), "kv6");
			if (!fs::IsFolder(defaultDirAbs))
				fs::CreateFolder(defaultDirAbs);
		}

		KV6ScreenHelper::~KV6ScreenHelper() {}

		std::vector<std::string> KV6ScreenHelper::GetFolders(const std::string& absDir) {
			return ListNames(absDir, true, [](const std::string&) { return true; });
		}

		std::vector<std::string> KV6ScreenHelper::GetFiles(const std::string& absDir) {
			return ListNames(absDir, false, IsModelFile);
		}

		bool KV6ScreenHelper::Exists(const std::string& absPath) { return fs::Exists(absPath); }
		bool KV6ScreenHelper::IsFolder(const std::string& absPath) { return fs::IsFolder(absPath); }

		int64_t KV6ScreenHelper::GetFileSize(const std::string& absPath) {
			return fs::GetFileSize(absPath);
		}

		bool KV6ScreenHelper::CreateFolder(const std::string& absPath) {
			return fs::CreateFolder(absPath);
		}

		bool KV6ScreenHelper::Delete(const std::string& absPath) { return fs::Delete(absPath); }

		bool KV6ScreenHelper::Rename(const std::string& absOld, const std::string& absNew) {
			return fs::Rename(absOld, absNew, false);
		}

		std::string KV6ScreenHelper::DefaultDir() {
			// Fall back to the app-data root if the kv6/ folder couldn't be created
			// (e.g. permissions, or the name is taken by a file), so the explorer
			// always opens somewhere valid.
			if (fs::IsFolder(defaultDirAbs))
				return defaultDirAbs;
			return std::string(spades::g_userResourceDirectory);
		}

		std::string KV6ScreenHelper::ParentDir(const std::string& absPath) {
			return fs::ParentDir(absPath);
		}

		VoxelModel* KV6ScreenHelper::Load(const std::string& absPath) {
			std::FILE* f = fs::OpenFile(absPath, "rb");
			if (!f)
				return nullptr;
			try {
				StdStream stream(f, true); // takes ownership of the FILE*
				return VoxelModel::LoadKV6(stream).Unmanage();
			} catch (const std::exception&) {
				return nullptr;
			}
		}

		bool KV6ScreenHelper::Save(VoxelModel* model, const std::string& absPath) {
			if (!model)
				return false;
			// Write to a sibling temp file first, then atomically replace the target,
			// so a failure mid-write can never truncate or corrupt an existing model.
			std::string tmpPath = absPath + ".savetmp";
			std::FILE* f = fs::OpenFile(tmpPath, "wb");
			if (!f)
				return false;
			try {
				StdStream stream(f, true); // takes ownership; closes/flushes at scope exit
				model->SaveKV6(stream);
			} catch (const std::exception&) {
				fs::Delete(tmpPath);
				return false;
			}
			// The StdStream above is destroyed (closing the file) before we replace
			// the target, so the rename sees a fully written, flushed temp file.
			if (!fs::Rename(tmpPath, absPath, true)) {
				fs::Delete(tmpPath);
				return false;
			}
			return true;
		}

	} // namespace gui
} // namespace spades
