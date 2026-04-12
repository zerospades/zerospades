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
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#include "Main.h"
#include <Core/StdStream.h>
#include <Core/VoxelModel.h>
#include <ScriptBindings/ScriptManager.h>

namespace spades {
	namespace gui {

		namespace {
			std::string ToLower(const std::string& s) {
				std::string out = s;
				std::transform(out.begin(), out.end(), out.begin(),
				               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return out;
			}

			bool HasExt(const std::string& s, const char* ext) {
				size_t n = std::strlen(ext);
				if (s.size() < n)
					return false;
				return ToLower(s).compare(s.size() - n, n, ext) == 0;
			}

			// Model files the explorer lists (.kv6 is editable; .2kv6/.vxl are
			// shown but not yet supported).
			bool IsModelFile(const std::string& s) {
				return HasExt(s, ".kv6") || HasExt(s, ".2kv6") || HasExt(s, ".vxl");
			}

			void MakeDir(const std::string& path) {
#ifdef _WIN32
				_mkdir(path.c_str());
#else
				::mkdir(path.c_str(), 0775);
#endif
			}

			bool IsDirAbs(const std::string& path) {
				struct stat st;
				if (::stat(path.c_str(), &st) != 0)
					return false;
				return (st.st_mode & S_IFDIR) != 0;
			}

			bool ExistsAbs(const std::string& path) {
				struct stat st;
				return ::stat(path.c_str(), &st) == 0;
			}

			// Join a directory and a name with a single separator.
			std::string Join(const std::string& dir, const std::string& name) {
				if (dir.empty())
					return name;
				char last = dir[dir.size() - 1];
				if (last == '/' || last == '\\')
					return dir + name;
				return dir + "/" + name;
			}

			// Directory entries (excluding "." / ".." and dotfiles).
			std::vector<std::string> ListDir(const std::string& path) {
				std::vector<std::string> out;
#ifdef _WIN32
				WIN32_FIND_DATAA fd;
				HANDLE h = FindFirstFileA((path + "\\*").c_str(), &fd);
				if (h == INVALID_HANDLE_VALUE)
					return out;
				do {
					if (fd.cFileName[0] == '.')
						continue;
					out.emplace_back(fd.cFileName);
				} while (FindNextFileA(h, &fd));
				FindClose(h);
#else
				DIR* d = ::opendir(path.c_str());
				if (!d)
					return out;
				while (auto* e = ::readdir(d)) {
					if (e->d_name[0] == '.')
						continue;
					out.emplace_back(e->d_name);
				}
				::closedir(d);
#endif
				return out;
			}

			CScriptArray* MakeStringArray(const std::vector<std::string>& items) {
				asIScriptEngine* eng = ScriptManager::GetInstance()->GetEngine();
				asITypeInfo* t = eng->GetTypeInfoByDecl("array<string>");
				CScriptArray* arr = CScriptArray::Create(t, static_cast<asUINT>(items.size()));
				for (size_t i = 0; i < items.size(); i++)
					arr->SetValue(static_cast<asUINT>(i), const_cast<std::string*>(&items[i]));
				return arr;
			}
		} // namespace

		KV6ScreenHelper::KV6ScreenHelper() {
			// Home is the user data folder (next to Mods/); the explorer can browse
			// freely up to the filesystem root from there.
			defaultDirAbs = std::string(spades::g_userResourceDirectory);
		}

		KV6ScreenHelper::~KV6ScreenHelper() {}

		CScriptArray* KV6ScreenHelper::GetFolders(const std::string& absDir) {
			std::vector<std::string> out;
			for (const std::string& name : ListDir(absDir)) {
				if (IsDirAbs(Join(absDir, name)))
					out.push_back(name);
			}
			std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
				return ToLower(a) < ToLower(b);
			});
			return MakeStringArray(out);
		}

		CScriptArray* KV6ScreenHelper::GetFiles(const std::string& absDir) {
			std::vector<std::string> out;
			for (const std::string& name : ListDir(absDir)) {
				if (IsModelFile(name) && !IsDirAbs(Join(absDir, name)))
					out.push_back(name);
			}
			std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
				return ToLower(a) < ToLower(b);
			});
			return MakeStringArray(out);
		}

		bool KV6ScreenHelper::Exists(const std::string& absPath) { return ExistsAbs(absPath); }
		bool KV6ScreenHelper::IsFolder(const std::string& absPath) { return IsDirAbs(absPath); }

		int64_t KV6ScreenHelper::GetFileSize(const std::string& absPath) {
			struct stat st;
			if (::stat(absPath.c_str(), &st) == 0)
				return static_cast<int64_t>(st.st_size);
			return -1;
		}

		bool KV6ScreenHelper::CreateFolder(const std::string& absPath) {
			if (absPath.empty() || ExistsAbs(absPath))
				return false;
			MakeDir(absPath);
			return IsDirAbs(absPath);
		}

		bool KV6ScreenHelper::Delete(const std::string& absPath) {
			if (absPath.empty() || !ExistsAbs(absPath))
				return false;
			if (IsDirAbs(absPath)) {
#ifdef _WIN32
				return _rmdir(absPath.c_str()) == 0;
#else
				return ::rmdir(absPath.c_str()) == 0; // only succeeds on empty dirs
#endif
			}
			return std::remove(absPath.c_str()) == 0;
		}

		bool KV6ScreenHelper::Rename(const std::string& absOld, const std::string& absNew) {
			if (absOld.empty() || absNew.empty())
				return false;
			if (!ExistsAbs(absOld) || ExistsAbs(absNew))
				return false;
			return std::rename(absOld.c_str(), absNew.c_str()) == 0;
		}

		std::string KV6ScreenHelper::DefaultDir() { return defaultDirAbs; }

		std::string KV6ScreenHelper::ParentDir(const std::string& absPath) {
			std::string p = absPath;
			// Drop trailing separators.
			while (p.size() > 1 && (p.back() == '/' || p.back() == '\\'))
				p.pop_back();
			size_t pos = p.find_last_of("/\\");
			if (pos == std::string::npos)
				return p; // no separator: already at a root-like path
			if (pos == 0)
				return "/"; // parent of "/foo" is the root
			return p.substr(0, pos);
		}

		VoxelModel* KV6ScreenHelper::Load(const std::string& absPath) {
			std::FILE* f = std::fopen(absPath.c_str(), "rb");
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
			std::FILE* f = std::fopen(absPath.c_str(), "wb");
			if (!f)
				return false;
			try {
				StdStream stream(f, true);
				model->SaveKV6(stream);
				return true;
			} catch (const std::exception&) {
				return false;
			}
		}

	} // namespace gui
} // namespace spades
