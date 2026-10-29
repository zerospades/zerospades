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
#include <set>
#include <string>
#include <vector>

#include "FileBrowserTypes.h"
#include <Gui/UI/Widgets/ListView.h>

namespace spades {
	namespace gui {
		class FileBrowserRow;

		/**
		 * The rows of one folder, plus the selection over them.
		 *
		 * Rows are supplied to a `ui::ListView`; a new model is built for every
		 * listing (as the other main-menu lists do), so it can be treated as an
		 * immutable snapshot of a folder with mutable selection state.
		 */
		class FileBrowserModel : public ui::ListViewModel {
		public:
			/** Reads `dir` and builds the rows the browser should show.
			 *
			 * Folders always come first, then files; both are sorted by name,
			 * case-insensitively. Files are listed only when `target` isn't
			 * `Folders` and `filter` matches them. Returns null and fills `error`
			 * if the folder cannot be read. */
			static Handle<FileBrowserModel> Build(ui::UIManager* manager, const std::string& dir,
			                                      FileBrowserTarget target, const FileFilter& filter,
			                                      bool showHidden, bool multiSelect,
			                                      const FileBrowserDescribeEntry& describe,
			                                      std::string* error);

			/** Raised whenever the selection changed (click, or `SelectByName`). */
			std::function<void()> selectionChanged;
			/** Raised on a double click; `row` is always a valid index. */
			std::function<void(int row)> rowActivated;

			int GetNumRows() override { return static_cast<int>(entries.size()); }
			Handle<ui::UIElement> CreateElement(int row) override;

			const std::vector<FileBrowserEntry>& GetEntries() const { return entries; }
			const FileBrowserEntry& GetEntry(int row) const { return entries[row]; }
			bool IsSelected(int row) const { return selection.count(row) > 0; }

			/** Selected rows, in listing order. */
			std::vector<int> GetSelectedRows() const;
			/** Convenience: the one selected row, or -1 if none or several. */
			int GetSingleSelectedRow() const;

			/** Applies a click with the given modifiers (Ctrl toggles, Shift
			 *  extends from the anchor; both are ignored without multi-select). */
			void ClickRow(int row, bool ctrlHeld, bool shiftHeld);
			void ClearSelection();
			/** Selects just `name` if it is listed; returns false if it is not. */
			bool SelectByName(const std::string& name);
			int FindRow(const std::string& name) const;

			/** Prefer `Build`, which reads a folder and sorts its entries. */
			FileBrowserModel(ui::UIManager* manager, std::vector<FileBrowserEntry> entries,
			                 bool multiSelect);

		protected:
			~FileBrowserModel();

		private:
			ui::UIManager* manager; // weak
			std::vector<FileBrowserEntry> entries;
			std::vector<Handle<FileBrowserRow>> rowElements;
			std::set<int> selection;
			int anchorRow = -1; // where a Shift range starts
			bool multiSelect;

			void OnRowClicked(ui::UIElement& sender);
			void OnRowDoubleClicked(ui::UIElement& sender);
			int RowIndexOf(ui::UIElement& sender) const;
		};
	} // namespace gui
} // namespace spades
