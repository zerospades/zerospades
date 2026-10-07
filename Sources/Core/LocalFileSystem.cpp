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

#include "LocalFileSystem.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "Strings.h"

namespace spades {
	namespace LocalFileSystem {
		namespace {
			char ToLowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

			void SetError(std::string* error, const std::string& message) {
				if (error)
					*error = message;
			}

#ifdef _WIN32
			std::wstring Widen(const std::string& s) {
				if (s.empty())
					return std::wstring();
				int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
				if (n <= 0)
					return std::wstring();
				std::wstring w(size_t(n), L'\0');
				MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
				return w;
			}

			std::string Narrow(const wchar_t* w) {
				int len = int(wcslen(w));
				if (len == 0)
					return std::string();
				int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
				if (n <= 0)
					return std::string();
				std::string s(size_t(n), '\0');
				WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, nullptr, nullptr);
				return s;
			}

			std::string OSErrorMessage(DWORD code) {
				wchar_t buf[512];
				DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				                         nullptr, code, 0, buf, DWORD(sizeof(buf) / sizeof(buf[0])),
				                         nullptr);
				if (n == 0)
					return "Error " + std::to_string(unsigned(code));
				std::string msg = Narrow(buf);
				while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n' ||
				                        msg.back() == ' ' || msg.back() == '.'))
					msg.pop_back();
				return msg;
			}

			std::string LastOSError() { return OSErrorMessage(GetLastError()); }

			int64_t FileTimeToUnix(const FILETIME& ft) {
				// FILETIME counts 100 ns intervals since 1601-01-01.
				const uint64_t kUnixEpoch = 116444736000000000ULL;
				uint64_t v = (uint64_t(ft.dwHighDateTime) << 32) | uint64_t(ft.dwLowDateTime);
				if (v < kUnixEpoch)
					return 0;
				return int64_t((v - kUnixEpoch) / 10000000ULL);
			}
#else
			std::string LastOSError() { return std::strerror(errno); }
#endif

			/**
			 * Length of the root prefix of `p`: 0 for a relative path, 1 for `/`,
			 * and on Windows 2-3 for `C:` / `C:\` or the `\\server\share` span.
			 */
			size_t RootLength(const std::string& p) {
#ifdef _WIN32
				if (p.size() >= 2 && IsSeparator(p[0]) && IsSeparator(p[1])) {
					// UNC: \\server\share
					size_t server = p.find_first_of("/\\", 2);
					if (server == std::string::npos)
						return p.size();
					size_t share = p.find_first_of("/\\", server + 1);
					return share == std::string::npos ? p.size() : share;
				}
				if (p.size() >= 2 && p[1] == ':' &&
				    ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')))
					return (p.size() >= 3 && IsSeparator(p[2])) ? 3 : 2;
#endif
				if (!p.empty() && IsSeparator(p[0]))
					return 1;
				return 0;
			}

			/** Index of the last separator at or after the root, or npos. */
			size_t LastSeparator(const std::string& p) {
				size_t root = RootLength(p);
				for (size_t i = p.size(); i > root; i--) {
					if (IsSeparator(p[i - 1]))
						return i - 1;
				}
				return std::string::npos;
			}
		} // namespace

		bool IsSeparator(char c) {
#ifdef _WIN32
			return c == '/' || c == '\\';
#else
			return c == '/';
#endif
		}

		bool ListDirectory(const std::string& dir, std::vector<DirEntry>& out, bool showHidden,
		                   std::string* error) {
			out.clear();
#ifdef _WIN32
			std::string pattern = dir;
			if (pattern.empty() || !IsSeparator(pattern.back()))
				pattern += '\\';
			pattern += '*';

			WIN32_FIND_DATAW fd;
			HANDLE h = FindFirstFileExW(Widen(pattern).c_str(), FindExInfoBasic, &fd,
			                            FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
			if (h == INVALID_HANDLE_VALUE) {
				DWORD code = GetLastError();
				// An empty drive root has no "." entry, so nothing matches at all.
				if (code == ERROR_FILE_NOT_FOUND && IsFolder(dir))
					return true;
				SetError(error, OSErrorMessage(code));
				return false;
			}
			do {
				if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
					continue;
				bool hidden = fd.cFileName[0] == L'.' ||
				              (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
				if (hidden && !showHidden)
					continue;
				DirEntry e;
				e.name = Narrow(fd.cFileName);
				e.isFolder = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
				e.size = e.isFolder ? -1
				                    : int64_t((uint64_t(fd.nFileSizeHigh) << 32) |
				                              uint64_t(fd.nFileSizeLow));
				e.modifiedTime = FileTimeToUnix(fd.ftLastWriteTime);
				out.push_back(std::move(e));
			} while (FindNextFileW(h, &fd));
			FindClose(h);
			return true;
#else
			DIR* d = ::opendir(dir.c_str());
			if (!d) {
				SetError(error, LastOSError());
				return false;
			}
			while (dirent* ent = ::readdir(d)) {
				const char* name = ent->d_name;
				if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0)
					continue;
				if (name[0] == '.' && !showHidden)
					continue;
				DirEntry e;
				e.name = name;
				// A dangling symlink cannot be stat'ed: list it as a file of unknown size.
				struct stat st;
				if (::stat(Join(dir, e.name).c_str(), &st) == 0) {
					e.isFolder = S_ISDIR(st.st_mode);
					e.size = e.isFolder ? -1 : int64_t(st.st_size);
					e.modifiedTime = int64_t(st.st_mtime);
				}
				out.push_back(std::move(e));
			}
			::closedir(d);
			return true;
#endif
		}

		bool GetEntryInfo(const std::string& path, DirEntry& out) {
			if (path.empty())
				return false;
#ifdef _WIN32
			WIN32_FILE_ATTRIBUTE_DATA data;
			if (!GetFileAttributesExW(Widen(path).c_str(), GetFileExInfoStandard, &data))
				return false;
			out.name = GetFileName(path);
			out.isFolder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			out.size = out.isFolder ? -1
			                        : int64_t((uint64_t(data.nFileSizeHigh) << 32) |
			                                  uint64_t(data.nFileSizeLow));
			out.modifiedTime = FileTimeToUnix(data.ftLastWriteTime);
			return true;
#else
			struct stat st;
			if (::stat(path.c_str(), &st) != 0)
				return false;
			out.name = GetFileName(path);
			out.isFolder = S_ISDIR(st.st_mode);
			out.size = out.isFolder ? -1 : int64_t(st.st_size);
			out.modifiedTime = int64_t(st.st_mtime);
			return true;
#endif
		}

		bool Exists(const std::string& path) {
			DirEntry e;
			return GetEntryInfo(path, e);
		}

		bool IsFolder(const std::string& path) {
			DirEntry e;
			return GetEntryInfo(path, e) && e.isFolder;
		}

		int64_t GetFileSize(const std::string& path) {
			DirEntry e;
			if (!GetEntryInfo(path, e) || e.isFolder)
				return -1;
			return e.size;
		}

		bool CreateFolder(const std::string& path, std::string* error) {
			if (path.empty()) {
				SetError(error, _Tr("LocalFileSystem", "The path is empty."));
				return false;
			}
			if (Exists(path)) {
				SetError(error, _Tr("LocalFileSystem", "An item with this name already exists."));
				return false;
			}
#ifdef _WIN32
			if (!CreateDirectoryW(Widen(path).c_str(), nullptr)) {
#else
			if (::mkdir(path.c_str(), 0775) != 0) {
#endif
				SetError(error, LastOSError());
				return false;
			}
			return true;
		}

		bool Delete(const std::string& path, std::string* error) {
			DirEntry e;
			if (!GetEntryInfo(path, e)) {
				SetError(error, _Tr("LocalFileSystem", "The item does not exist."));
				return false;
			}
#ifdef _WIN32
			std::wstring w = Widen(path);
			bool ok = e.isFolder ? RemoveDirectoryW(w.c_str()) != 0 : DeleteFileW(w.c_str()) != 0;
#else
			bool ok = e.isFolder ? ::rmdir(path.c_str()) == 0 : ::unlink(path.c_str()) == 0;
#endif
			if (!ok)
				SetError(error, LastOSError());
			return ok;
		}

		bool Rename(const std::string& from, const std::string& to, bool replaceExisting,
		            std::string* error) {
			if (from.empty() || to.empty()) {
				SetError(error, _Tr("LocalFileSystem", "The path is empty."));
				return false;
			}
			if (!Exists(from)) {
				SetError(error, _Tr("LocalFileSystem", "The item does not exist."));
				return false;
			}
			if (!replaceExisting && Exists(to)) {
				SetError(error, _Tr("LocalFileSystem", "An item with this name already exists."));
				return false;
			}
#ifdef _WIN32
			DWORD flags = MOVEFILE_WRITE_THROUGH;
			if (replaceExisting)
				flags |= MOVEFILE_REPLACE_EXISTING;
			if (!MoveFileExW(Widen(from).c_str(), Widen(to).c_str(), flags)) {
#else
			if (std::rename(from.c_str(), to.c_str()) != 0) {
#endif
				SetError(error, LastOSError());
				return false;
			}
			return true;
		}

		std::FILE* OpenFile(const std::string& path, const char* mode) {
#ifdef _WIN32
			return _wfopen(Widen(path).c_str(), Widen(mode).c_str());
#else
			return std::fopen(path.c_str(), mode);
#endif
		}

		std::string Join(const std::string& dir, const std::string& name) {
			if (dir.empty())
				return name;
			if (IsSeparator(dir.back()))
				return dir + name;
#ifdef _WIN32
			// Follow the style the directory already uses.
			if (dir.find('\\') != std::string::npos && dir.find('/') == std::string::npos)
				return dir + "\\" + name;
#endif
			return dir + "/" + name;
		}

		std::string StripTrailingSeparators(const std::string& path) {
			std::string p = path;
			size_t root = RootLength(p);
			while (p.size() > root && IsSeparator(p.back()))
				p.pop_back();
			return p;
		}

		std::string ParentDir(const std::string& path) {
			std::string p = StripTrailingSeparators(path);
			size_t root = RootLength(p);
			if (p.size() <= root)
				return p; // already a root (or empty)
			size_t sep = LastSeparator(p);
			if (sep == std::string::npos)
				return root > 0 ? p.substr(0, root) : p;
			// Keep a root's own separator ("/foo" -> "/", "C:\foo" -> "C:\").
			return p.substr(0, std::max(sep, root));
		}

		bool IsRoot(const std::string& path) {
			std::string p = StripTrailingSeparators(path);
			size_t root = RootLength(p);
			return root > 0 && p.size() == root;
		}

		std::string GetFileName(const std::string& path) {
			std::string p = StripTrailingSeparators(path);
			if (p.size() <= RootLength(p))
				return p;
			size_t sep = LastSeparator(p);
			if (sep == std::string::npos)
				return p.substr(RootLength(p));
			return p.substr(sep + 1);
		}

		bool HasExtension(const std::string& name, const std::string& extension) {
			if (name.size() < extension.size())
				return false;
			size_t offset = name.size() - extension.size();
			for (size_t i = 0; i < extension.size(); i++) {
				if (ToLowerAscii(name[offset + i]) != ToLowerAscii(extension[i]))
					return false;
			}
			return true;
		}

		std::vector<std::string> GetRoots() {
			std::vector<std::string> roots;
#ifdef _WIN32
			wchar_t buf[512];
			DWORD n = GetLogicalDriveStringsW(DWORD(sizeof(buf) / sizeof(buf[0])), buf);
			if (n > 0 && n < sizeof(buf) / sizeof(buf[0])) {
				// A sequence of NUL-terminated strings ("C:\", "D:\", ...), ending in "".
				for (const wchar_t* p = buf; *p; p += wcslen(p) + 1)
					roots.push_back(Narrow(p));
			}
#else
			roots.push_back("/");
#endif
			return roots;
		}

		bool IsValidFileName(const std::string& name, std::string* reason) {
			if (name.empty()) {
				SetError(reason, _Tr("LocalFileSystem", "The name is empty."));
				return false;
			}
			if (name == "." || name == "..") {
				SetError(reason, _Tr("LocalFileSystem", "This name is reserved."));
				return false;
			}
			for (char c : name) {
				if (static_cast<unsigned char>(c) < 0x20 || std::strchr("/\\<>:\"|?*", c)) {
					SetError(reason, _Tr("LocalFileSystem",
					                     "Names cannot contain control characters or any of / \\ < > : \" | ? *"));
					return false;
				}
			}
			if (name.back() == '.' || name.back() == ' ') {
				SetError(reason, _Tr("LocalFileSystem", "Names cannot end with a dot or a space."));
				return false;
			}

			// Windows device names are reserved regardless of extension ("nul.kv6").
			std::string base = name.substr(0, name.find('.'));
			for (char& c : base)
				c = ToLowerAscii(c);
			static const char* const kReserved[] = {"con", "prn", "aux", "nul"};
			bool reserved = false;
			for (const char* r : kReserved)
				reserved = reserved || base == r;
			if (base.size() == 4 && (base.compare(0, 3, "com") == 0 || base.compare(0, 3, "lpt") == 0) &&
			    base[3] >= '1' && base[3] <= '9')
				reserved = true;
			if (reserved) {
				SetError(reason, _Tr("LocalFileSystem", "This name is reserved by the system."));
				return false;
			}
			return true;
		}
	} // namespace LocalFileSystem
} // namespace spades
