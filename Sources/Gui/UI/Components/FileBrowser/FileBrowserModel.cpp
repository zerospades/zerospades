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

#include "FileBrowserModel.h"
#include "FileBrowserRow.h"

#include <algorithm>

#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		using ui::UIElement;
		using ui::UIManager;

		namespace {
			std::string ToLowerAscii(const std::string& s) {
				std::string out = s;
				for (char& c : out)
					c = (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
				return out;
			}

			// Folders first, then files; each group by name, case-insensitively.
			bool EntryLess(const FileBrowserEntry& a, const FileBrowserEntry& b) {
				if (a.isFolder != b.isFolder)
					return a.isFolder;
				return ToLowerAscii(a.name) < ToLowerAscii(b.name);
			}
		} // namespace

		Handle<FileBrowserModel> FileBrowserModel::Build(UIManager* manager, const std::string& dir,
		                                                 FileBrowserTarget target,
		                                                 const FileFilter& filter, bool showHidden,
		                                                 bool multiSelect,
		                                                 const FileBrowserDescribeEntry& describe,
		                                                 std::string* error) {
			std::vector<LocalFileSystem::DirEntry> listing;
			if (!LocalFileSystem::ListDirectory(dir, listing, showHidden, error))
				return Handle<FileBrowserModel>();

			std::vector<FileBrowserEntry> entries;
			entries.reserve(listing.size());
			for (const LocalFileSystem::DirEntry& e : listing) {
				// Folders are always listed, even when only files may be picked:
				// they are how the user navigates.
				if (!e.isFolder) {
					if (target == FileBrowserTarget::Folders || !filter.Matches(e.name))
						continue;
				}

				FileBrowserEntry entry;
				entry.name = e.name;
				entry.isFolder = e.isFolder;
				entry.size = e.size;
				entry.modifiedTime = e.modifiedTime;
				entry.accepted = e.isFolder ? target != FileBrowserTarget::Files
				                            : target != FileBrowserTarget::Folders;
				if (describe) {
					FileEntryInfo info = describe(e);
					entry.accepted = entry.accepted && info.accepted;
					entry.hint = info.hint;
				}
				entries.push_back(std::move(entry));
			}
			std::sort(entries.begin(), entries.end(), EntryLess);

			return Handle<FileBrowserModel>::New(manager, std::move(entries), multiSelect);
		}

		FileBrowserModel::FileBrowserModel(UIManager* manager, std::vector<FileBrowserEntry> entries,
		                                   bool multiSelect)
		    : manager(manager), entries(std::move(entries)), multiSelect(multiSelect) {
			rowElements.resize(this->entries.size());
		}

		FileBrowserModel::~FileBrowserModel() {}

		Handle<UIElement> FileBrowserModel::CreateElement(int row) {
			if (!rowElements[row]) {
				Handle<FileBrowserRow> element = Handle<FileBrowserRow>::New(manager, this, row);
				element->activated = [this](UIElement& s) { OnRowClicked(s); };
				element->doubleClicked = [this](UIElement& s) { OnRowDoubleClicked(s); };
				rowElements[row] = element;
			}
			return rowElements[row].Cast<UIElement>();
		}

		int FileBrowserModel::RowIndexOf(UIElement& sender) const {
			FileBrowserRow* element = dynamic_cast<FileBrowserRow*>(&sender);
			if (!element)
				return -1;
			int row = element->GetRow();
			return (row >= 0 && row < static_cast<int>(entries.size())) ? row : -1;
		}

		void FileBrowserModel::OnRowClicked(UIElement& sender) {
			int row = RowIndexOf(sender);
			if (row < 0)
				return;
			// Cmd stands in for Ctrl on macOS, matching the text fields.
			bool ctrl = manager->isControlPressed || manager->isMetaPressed;
			ClickRow(row, ctrl, manager->isShiftPressed);
		}

		void FileBrowserModel::OnRowDoubleClicked(UIElement& sender) {
			// A handler may replace this model (e.g. by entering a folder), which
			// would drop the last reference to it while it is still running.
			Handle<FileBrowserModel> keepAlive(this);
			int row = RowIndexOf(sender);
			if (row >= 0 && rowActivated)
				rowActivated(row);
		}

		void FileBrowserModel::ClickRow(int row, bool ctrlHeld, bool shiftHeld) {
			Handle<FileBrowserModel> keepAlive(this);
			if (row < 0 || row >= static_cast<int>(entries.size()))
				return;

			if (multiSelect && ctrlHeld) {
				if (!selection.erase(row))
					selection.insert(row);
				anchorRow = row;
			} else if (multiSelect && shiftHeld && anchorRow >= 0) {
				selection.clear();
				int from = std::min(anchorRow, row);
				int to = std::max(anchorRow, row);
				for (int i = from; i <= to; i++)
					selection.insert(i);
			} else {
				selection.clear();
				selection.insert(row);
				anchorRow = row;
			}

			if (selectionChanged)
				selectionChanged();
		}

		void FileBrowserModel::ClearSelection() {
			Handle<FileBrowserModel> keepAlive(this);
			if (selection.empty())
				return;
			selection.clear();
			anchorRow = -1;
			if (selectionChanged)
				selectionChanged();
		}

		bool FileBrowserModel::SelectByName(const std::string& name) {
			int row = FindRow(name);
			if (row < 0)
				return false;
			ClickRow(row, false, false);
			return true;
		}

		int FileBrowserModel::FindRow(const std::string& name) const {
			for (size_t i = 0; i < entries.size(); i++) {
				if (entries[i].name == name)
					return static_cast<int>(i);
			}
			return -1;
		}

		std::vector<int> FileBrowserModel::GetSelectedRows() const {
			return std::vector<int>(selection.begin(), selection.end()); // std::set: ordered
		}

		int FileBrowserModel::GetSingleSelectedRow() const {
			return selection.size() == 1 ? *selection.begin() : -1;
		}
	} // namespace gui
} // namespace spades
