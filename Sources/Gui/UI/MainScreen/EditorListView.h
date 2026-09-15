/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <Gui/UI/Widgets/ButtonBase.h>
#include <Gui/UI/Widgets/ListView.h>

namespace spades {
	namespace gui {
		/** True if `name` is a directly editable model file (currently `.kv6`). */
		bool EditorIsEditable(const std::string& name);

		/** True if `name` is a recognized voxel model file (`.kv6`/`.2kv6`/`.vxl`). */
		bool EditorIsModelFile(const std::string& name);

		/** One entry in the file browser: a folder or a model file. */
		struct EditorEntry {
			std::string name;
			bool isFolder = false;
		};

		/** One clickable row of the file browser. */
		class EditorListItem : public ui::ButtonBase {
			EditorEntry entry;

		public:
			EditorListItem(ui::UIManager* manager, EditorEntry entry);

			const EditorEntry& GetEntry() const { return entry; }

			void Render() override;
		};

		using EditorListItemEventHandler =
		    std::function<void(const std::string& name, bool isFolder)>;

		/** `ListViewModel` backing the file browser (folders first, then files). */
		class EditorListModel : public ui::ListViewModel {
			ui::UIManager* manager; // weak
			std::vector<EditorEntry> entries;
			std::vector<Handle<EditorListItem>> itemElements;

			void OnItemClicked(ui::UIElement& sender);
			void OnItemDoubleClicked(ui::UIElement& sender);

		public:
			EditorListItemEventHandler itemActivated;
			EditorListItemEventHandler itemDoubleClicked;

			EditorListModel(ui::UIManager* manager, std::vector<EditorEntry> entries);

			int GetNumRows() override { return static_cast<int>(entries.size()); }
			Handle<ui::UIElement> CreateElement(int row) override;
		};
	} // namespace gui
} // namespace spades
