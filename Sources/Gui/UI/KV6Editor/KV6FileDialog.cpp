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

#include "KV6FileDialog.h"

#include <Gui/ModelFileTypes.h>
#include <Core/LocalFileSystem.h>
#include <Core/Settings.h>

DEFINE_SPADES_SETTING(cl_kv6EditorFolder, ""); // remembered folder (absolute)

namespace spades {
	namespace gui {
		namespace fs = LocalFileSystem;

		FileBrowserOptions KV6ModelBrowserOptions() {
			FileBrowserOptions options;
			options.target = FileBrowserTarget::Files;
			options.filters.push_back(FileFilter{KV6ModelFilterLabel(), KV6ModelExtensions()});
			options.appendFilterExtension = true;
			options.allowCreateFolder = true;
			options.allowRename = true;
			options.allowDelete = true;

			options.describeEntry = [](const fs::DirEntry& entry) {
				FileEntryInfo info;
				if (!entry.isFolder && !KV6IsEditable(entry.name)) {
					info.accepted = false;
					info.hint = KV6UnsupportedHint();
				}
				return info;
			};

			return options;
		}

		std::string KV6RememberedFolder(const std::string& fallbackDir) {
			std::string remembered = static_cast<std::string>(cl_kv6EditorFolder);
			return remembered.empty() ? fallbackDir : remembered;
		}

		void KV6RememberFolder(const std::string& directory) {
			if (!directory.empty())
				cl_kv6EditorFolder = directory;
		}
	} // namespace gui
} // namespace spades
