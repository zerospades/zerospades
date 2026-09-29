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

#include "EditorFileDialog.h"

#include <Gui/DocumentTypes.h>
#include <Core/LocalFileSystem.h>
#include <Core/Settings.h>
#include <Gui/Main.h>
#include <Gui/UI/Components/FileBrowser/FileBrowserDialog.h>
#include <Gui/UI/Components/UIOverlayHost.h>

DEFINE_SPADES_SETTING(cl_editorFolder, ""); // remembered folder (absolute)

namespace spades {
	namespace gui {
		namespace fs = LocalFileSystem;

		FileBrowserOptions EditorBrowserOptions() {
			FileBrowserOptions options;
			options.target = FileBrowserTarget::Files;
			options.filters.push_back(FileFilter{DocumentFilterLabel(), DocumentExtensions()});
			options.appendFilterExtension = true;
			options.allowCreateFolder = true;
			options.allowRename = true;
			options.allowDelete = true;

			options.describeEntry = [](const fs::DirEntry& entry) {
				FileEntryInfo info;
				if (!entry.isFolder && !IsEditableDocument(entry.name)) {
					info.accepted = false;
					info.hint = UnsupportedDocumentHint();
				}
				return info;
			};

			return options;
		}

		std::string EditorHomeDir() {
			// The app-data folder exists (the game makes it at startup), so one
			// level is all there is to make.
			const std::string home =
			  fs::Join(std::string(spades::g_userResourceDirectory), "kv6");
			if (!fs::IsFolder(home))
				fs::CreateFolder(home);
			return fs::IsFolder(home) ? home : std::string(spades::g_userResourceDirectory);
		}

		std::string EditorRememberedFolder(const std::string& fallbackDir) {
			std::string remembered = static_cast<std::string>(cl_editorFolder);
			return remembered.empty() ? fallbackDir : remembered;
		}

		void EditorRememberFolder(const std::string& directory) {
			if (!directory.empty())
				cl_editorFolder = directory;
		}

		void ShowEditorFileDialog(UIOverlayHost& overlay, const std::string& title,
		                          FileBrowserOptions options,
		                          std::function<void(const std::string&)> picked) {
			Handle<FileBrowserDialog> dialog = Handle<FileBrowserDialog>::New(
			  &overlay.GetUIManager().GetRootElement(), title, std::move(options));
			dialog->closed = [picked](const FileBrowserResult& result) {
				// Where the player ended up is where every editor dialog opens next,
				// whether or not they picked something here.
				EditorRememberFolder(result.directory);
				if (result.accepted && !result.paths.empty() && picked)
					picked(result.paths.front());
			};
			overlay.Show(dialog.GetPointerOrNull());
		}
	} // namespace gui
} // namespace spades
