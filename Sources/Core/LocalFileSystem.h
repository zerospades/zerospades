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

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace spades {
	/**
	 * Direct access to the host file system by native path, UTF-8 encoded on
	 * every platform.
	 *
	 * Unlike `FileManager` / `IFileSystem`, which resolve logical paths through
	 * the game's mounted resource roots, this touches the real disk. It backs
	 * tools that let the user pick locations anywhere (file browsers, editors).
	 *
	 * Operations that can fail return false and, when `error` is non-null, store
	 * a human-readable reason (usually the OS's own message).
	 *
	 * Path separators: `/` everywhere; on Windows `\` is accepted too, and drive
	 * (`C:\`) and UNC (`\\server\share`) roots are recognised.
	 */
	namespace LocalFileSystem {
		struct DirEntry {
			std::string name;         // entry name only, without the directory
			bool isFolder = false;
			int64_t size = -1;        // file size in bytes; -1 for folders or if unknown
			int64_t modifiedTime = 0; // seconds since the Unix epoch; 0 if unknown
		};

		/**
		 * Lists the entries directly inside `dir` (never `.` / `..`), in no
		 * particular order. Hidden entries (dotfiles, and on Windows entries with
		 * the hidden attribute) are skipped unless `showHidden`. Returns false if
		 * the directory cannot be opened; `out` is cleared either way.
		 */
		bool ListDirectory(const std::string& dir, std::vector<DirEntry>& out, bool showHidden,
		                   std::string* error = nullptr);

		/** Fills `out` for the entry at `path` (its `name` is the last component). */
		bool GetEntryInfo(const std::string& path, DirEntry& out);
		bool Exists(const std::string& path);
		bool IsFolder(const std::string& path);
		/** Size in bytes of the file at `path`; -1 if missing or a folder. */
		int64_t GetFileSize(const std::string& path);

		/** Creates one folder; fails if anything already exists at `path`. */
		bool CreateFolder(const std::string& path, std::string* error = nullptr);
		/** Deletes a file or an empty folder. Never recurses. */
		bool Delete(const std::string& path, std::string* error = nullptr);
		/**
		 * Renames/moves `from` to `to`. Unless `replaceExisting`, fails if `to`
		 * already exists; with it, an existing file is atomically replaced.
		 */
		bool Rename(const std::string& from, const std::string& to, bool replaceExisting,
		            std::string* error = nullptr);

		/** `fopen` that understands UTF-8 paths on Windows. */
		std::FILE* OpenFile(const std::string& path, const char* mode);

		/** True if `c` separates path components on this platform. */
		bool IsSeparator(char c);
		/** Joins with exactly one separator (matching the style already in `dir`). */
		std::string Join(const std::string& dir, const std::string& name);
		/** Drops trailing separators, but never shortens a root (`/`, `C:\`). */
		std::string StripTrailingSeparators(const std::string& path);
		/** The containing folder; a root (or a bare relative name) is returned as is. */
		std::string ParentDir(const std::string& path);
		/** True if `path` names a file system root. */
		bool IsRoot(const std::string& path);
		/** The last path component (a root is returned as is). */
		std::string GetFileName(const std::string& path);
		/** Case-insensitive (ASCII) test for a suffix such as ".kv6". */
		bool HasExtension(const std::string& name, const std::string& extension);

		/** The roots the user can browse from: `/`, or every drive on Windows. */
		std::vector<std::string> GetRoots();

		/**
		 * True if `name` can be used as a single file or folder name. The rules
		 * are the portable (Windows) ones on every platform, so files created
		 * here can be shared across systems.
		 */
		bool IsValidFileName(const std::string& name, std::string* reason = nullptr);
	} // namespace LocalFileSystem
} // namespace spades
