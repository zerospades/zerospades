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

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <Gui/UI/Widgets/ButtonBase.h>
#include <Gui/UI/Widgets/ListView.h>

namespace spades {
	namespace client {
		class IRenderer;
		class IFont;
	} // namespace client
	namespace gui {
		class ModsScreenHelper;

		using ModListItemEventHandler = std::function<void(const std::string& modName)>;

		/** One row of the mod manager list. */
		class ModListItem : public ui::ButtonBase {
			std::string category;    // e.g. SEMI/SMG/...; empty when the name is unstructured
			std::string displayName; // parsed name, or full filename when unstructured
			std::string author;      // empty when unstructured
			std::int64_t totalSize;
			bool enabled; // present in the apply history
			bool exists;  // mod still present on disk
			int orderNum; // 1-based apply position, 0 when disabled
			float checkColWidth;
			float orderColWidth;
			float tagColWidth;
			float nameColWidth;
			float authorColWidth;

			// Draw the tag cell: a weapon icon for weapon categories, a text pill
			// for other structured categories, nothing for an unstructured name.
			void RenderTag(client::IRenderer& r, client::IFont* font, float cellX, float cellY,
			               float cellH, const Vector4& fgcolor);

		public:
			std::string modName;

			ModListItem(ui::UIManager* manager, const std::string& modName,
			            const std::string& category, const std::string& displayName,
			            const std::string& author, std::int64_t totalSize, bool enabled, bool exists,
			            int orderNum, float checkColWidth, float orderColWidth, float tagColWidth,
			            float nameColWidth, float authorColWidth, float sizeColWidth);
			void Render() override;
		};

		/** `ListViewModel` for the mod manager. */
		class ModListModel : public ui::ListViewModel {
			ui::UIManager* manager;  // weak
			ModsScreenHelper* helper; // weak
			std::vector<std::string> list;
			std::vector<int> orders;  // parallel to list: 1-based apply position, 0 if disabled
			std::vector<bool> exists; // parallel to list: mod still present on disk
			float checkColWidth;
			float orderColWidth;
			float tagColWidth;
			float nameColWidth;
			float authorColWidth;
			float sizeColWidth;
			std::vector<Handle<ModListItem>> itemElements;

			void OnItemClicked(ui::UIElement& sender);

		public:
			ModListItemEventHandler itemActivated;

			ModListModel(ui::UIManager* manager, ModsScreenHelper* helper,
			             std::vector<std::string> list, std::vector<int> orders,
			             std::vector<bool> exists, float checkColWidth, float orderColWidth,
			             float tagColWidth, float nameColWidth, float authorColWidth,
			             float sizeColWidth);

			int GetNumRows() override { return static_cast<int>(list.size()); }
			Handle<ui::UIElement> CreateElement(int row) override;
		};

		/** A static column header for the mod manager. */
		class ModListHeader : public ui::UIElement {
		public:
			std::string text;

			ModListHeader(ui::UIManager* manager) : ui::UIElement(manager) {}
			void Render() override;
		};

		/** Simple fill-bar progress indicator; fraction is clamped to [0, 1]. */
		class ModsProgressBar : public ui::UIElement {
		public:
			float fraction = 0.0F;

			ModsProgressBar(ui::UIManager* manager) : ui::UIElement(manager) {}
			void Render() override;
		};
	} // namespace gui
} // namespace spades
