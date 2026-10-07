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

#include <functional>
#include <string>
#include <vector>

#include "FileBrowserModel.h"
#include "FileBrowserTypes.h"
#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		namespace ui {
			class Button;
			class Field;
			class Label;
			class ListView;
		} // namespace ui

		/** An extra button the owner adds to the browser's action row. */
		struct FileBrowserAction {
			std::string caption;
			float width = 105.0F;
			/** Called on click. */
			std::function<void()> run;
			/** Optional; when it returns false the button is greyed out. */
			std::function<bool()> isEnabled;
		};

		/** How one browser instance behaves; see `FileBrowserView`. */
		struct FileBrowserOptions {
			FileBrowserPurpose purpose = FileBrowserPurpose::Open;
			FileBrowserTarget target = FileBrowserTarget::Files;
			/** Open only; Save and Create always return exactly one path. */
			bool multiSelect = false;

			/** Where to start; falls back to the nearest existing parent, then home. */
			std::string initialDir;
			/** Where the Home button goes; the first file system root if empty. */
			std::string homeDir;
			/** Save/Create: the name the field starts with. */
			std::string initialName;
			/** Empty caption means "Open", "Save" or "Create" to match `purpose`. */
			std::string confirmCaption;

			/** First entry is the default; empty means every file is listed. */
			std::vector<FileFilter> filters;
			/** Save/Create: append the active filter's extension if the typed name
			 *  has none of them. */
			bool appendFilterExtension = true;
			bool showHidden = false;

			bool allowCreateFolder = true;
			bool allowRename = false;
			bool allowDelete = false;
			/** Confirm/Cancel, the name field and the filter button. Embedded
			 *  panels (a menu tab) turn this off and drive the browser themselves. */
			bool showFooter = true;
			/** Draws a "Name" column header above the list. */
			bool showListHeader = false;
			/** Space between the action row and the list (or its header). Lets an
			 *  embedded browser line up with a screen's existing rows. */
			float listTopGap = 5.0F;

			std::vector<FileBrowserAction> extraActions;
			FileBrowserDescribeEntry describeEntry;
			/**
			 * Last check before the browser closes. A non-empty message is shown
			 * inside the browser and the paths are not accepted.
			 */
			std::function<std::string(const std::vector<std::string>& paths)> validate;
		};

		/**
		 * A reusable file browser: path bar, folder listing, action buttons and an
		 * optional footer with a name field, filter and Confirm/Cancel.
		 *
		 * It knows nothing about what is being browsed: file types, extra buttons
		 * and validation all come from `FileBrowserOptions`, and the result is
		 * reported through `accepted`. It lays itself out from its own bounds, so
		 * it works both embedded in a panel and inside a modal dialog.
		 */
		class FileBrowserView : public ui::UIElement {
		public:
			/**
			 * `modalOwner` is the element that prompts (rename, overwrite, errors)
			 * disable while they are up; it defaults to this view. It must be
			 * attached to a parent, as the prompts attach themselves next to it.
			 */
			FileBrowserView(ui::UIManager* manager, FileBrowserOptions options,
			                ui::UIElement* modalOwner = nullptr);

			/** Confirmed: the chosen paths, and the folder to remember. */
			std::function<void(const FileBrowserResult&)> accepted;
			/** Cancel pressed, or Escape in a modal. */
			std::function<void()> cancelled;
			/** The browser moved to another folder. */
			std::function<void(const std::string& directory)> directoryChanged;
			/**
			 * A row the caller marked as not accepted was chosen. Without a handler
			 * the browser shows the entry's hint as an inline message.
			 */
			std::function<void(const FileBrowserEntry& entry)> entryRejected;
			/**
			 * A path typed in the path bar that does not exist. Set this to accept
			 * such a path anyway (creating a document, for instance); without it the
			 * browser reports that there is no such file.
			 */
			std::function<void(const std::string& path)> unknownPathSubmitted;

			const std::string& GetDirectory() const { return dir; }
			/** Names of the currently selected entries (may be empty). */
			std::vector<std::string> GetSelectedNames() const;

			/** Re-reads the current folder, keeping the selection where possible. */
			void Refresh();
			/** Reacts to Enter as if the browser had keyboard focus (for an embedded
			 *  browser whose host screen sees the key first). */
			void SubmitDefault();
			/** Moves to `path` if it is a folder; otherwise shows a message. */
			void Navigate(const std::string& path);
			/** Acts on the path bar: enter a folder, or accept a typed file name. */
			void SubmitPath();
			/** Applies the purpose's rules and reports the result if they pass. */
			void Confirm();
			void Cancel();

			void OnResized() override;
			void HotKey(const std::string& key) override;

		private:
			FileBrowserOptions options;
			ui::UIElement* modalOwner; // weak

			ui::Field* pathField = nullptr;  // weak; owned as a child
			ui::Field* nameField = nullptr;  // weak; null unless Save/Create
			ui::ListView* list = nullptr;    // weak
			ui::Label* header = nullptr;     // weak; null unless showListHeader
			ui::Label* errorLabel = nullptr; // weak
			ui::Button* homeButton = nullptr;
			ui::Button* upButton = nullptr;
			ui::Button* newFolderButton = nullptr;
			ui::Button* renameButton = nullptr;
			ui::Button* deleteButton = nullptr;
			ui::Button* confirmButton = nullptr;
			ui::Button* cancelButton = nullptr;
			ui::Button* filterButton = nullptr;
			std::vector<ui::Button*> actionButtons; // weak; parallel to extraActions
			Handle<FileBrowserModel> model;

			std::string dir;
			std::string listedDir;   // folder the current listing came from
			std::string notifiedDir; // folder last reported through directoryChanged
			size_t filterIndex = 0;
			std::string pendingSavePath; // awaiting the overwrite confirmation

			const FileFilter& ActiveFilter() const;
			std::string DefaultConfirmCaption() const;
			std::string Child(const std::string& name) const;

			/** Lists `target`; on success it becomes the current folder. */
			bool LoadDirectory(const std::string& target);
			void BuildWidgets();
			void LayoutWidgets();
			void UpdateButtons();
			void SetError(const std::string& message);
			/** The folder the Home button leads to (falls back to a root). */
			std::string HomeDirectory() const;
			/** Reports an entry the owner refuses, through `entryRejected` or inline. */
			void RejectEntry(const FileBrowserEntry& entry);
			/** Shows `message` in a modal alert (used for failures that need to be
			 *  acknowledged, e.g. a delete that the OS refused). */
			void ShowAlert(const std::string& message);

			void OnSelectionChanged();
			void OnRowActivated(int row);
			void OnHome();
			void OnUp();
			void OnNewFolder();
			void OnNewFolderClosed(ui::UIElement& sender);
			void OnRename();
			void OnDelete();
			void OnDeleteClosed(ui::UIElement& sender);
			void OnFilterCycle();
			void OnOverwriteClosed(ui::UIElement& sender);

			/** Open: collect the selected paths. Returns false (with a message
			 *  shown) if the selection cannot be accepted. */
			bool CollectOpenPaths(std::vector<std::string>& paths);
			/** Save/Create: build the single path from the name field. */
			bool CollectTypedPath(std::string& path, bool& needsOverwriteConfirm);
			void Finish(std::vector<std::string> paths);
		};
	} // namespace gui
} // namespace spades
