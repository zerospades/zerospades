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
#include <functional>
#include <string>
#include <vector>

#include <Core/LocalFileSystem.h>

namespace spades {
	namespace gui {
		/** What the browser is being used for. */
		enum class FileBrowserPurpose {
			Open,  // pick existing entries
			Save,  // an existing entry (overwrite, after confirming) or a new name
			Create // a new name only; an existing entry is rejected
		};

		/** What kind of entry the browser returns. */
		enum class FileBrowserTarget {
			Files,          // folders stay navigable but are never the result
			Folders,        // files are not listed
			FilesAndFolders // either may be returned
		};

		/** One named group of extensions offered in the filter list. */
		struct FileFilter {
			std::string label;                   // "KV6 models"
			std::vector<std::string> extensions; // {".kv6"}; empty means every file

			bool Matches(const std::string& name) const {
				if (extensions.empty())
					return true;
				for (const std::string& ext : extensions) {
					if (LocalFileSystem::HasExtension(name, ext))
						return true;
				}
				return false;
			}
		};

		/** What the owner says about one listed entry. */
		struct FileEntryInfo {
			/** False greys the row out and refuses it as a result (it can still be
			 *  selected, so it can be renamed or deleted, and folders entered). */
			bool accepted = true;
			/** Shown after the name, e.g. "(not implemented)". */
			std::string hint;
		};

		/** Describes one entry; the default accepts everything with no hint. */
		using FileBrowserDescribeEntry =
		  std::function<FileEntryInfo(const LocalFileSystem::DirEntry& entry)>;

		/** One row of the browser's list. */
		struct FileBrowserEntry {
			std::string name;
			bool isFolder = false;
			int64_t size = -1;
			int64_t modifiedTime = 0;
			bool accepted = true;
			std::string hint;
		};

		/** What the browser hands back when it is confirmed. */
		struct FileBrowserResult {
			bool accepted = false;
			/** Absolute paths; one entry unless multi-select is on. */
			std::vector<std::string> paths;
			/** The folder the browser ended in, for the caller to remember. */
			std::string directory;
		};
	} // namespace gui
} // namespace spades
