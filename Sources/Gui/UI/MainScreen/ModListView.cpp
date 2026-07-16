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

#include "ModListView.h"
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/ModsScreenHelper.h>
#include <Gui/UI/Framework/TextUtils.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		using ui::FormatFileSize;
		using ui::SetColorNP;
		using ui::UIElement;
		using ui::UIManager;

		// -- ModListItem --

		ModListItem::ModListItem(UIManager* manager, const std::string& modName, int pakCount,
		                         std::int64_t totalSize, bool enabled, bool exists, int orderNum,
		                         float checkColWidth, float orderColWidth, float nameColWidth,
		                         float countColWidth, float sizeColWidth)
		    : ui::ButtonBase(manager),
		      pakCount(pakCount),
		      totalSize(totalSize),
		      enabled(enabled),
		      exists(exists),
		      orderNum(orderNum),
		      checkColWidth(checkColWidth),
		      orderColWidth(orderColWidth),
		      nameColWidth(nameColWidth),
		      countColWidth(countColWidth),
		      sizeColWidth(sizeColWidth),
		      modName(modName) {}

		void ModListItem::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			Vector4 bgcolor = MakeVector4(1.0F, 1.0F, 1.0F, 0.0F);
			// White when disabled, green when enabled, orange when enabled but
			// the mod is no longer on disk.
			Vector4 fgcolor = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			if (enabled)
				fgcolor = exists ? MakeVector4(0.4F, 1.0F, 0.4F, 1.0F)
				                 : MakeVector4(1.0F, 0.62F, 0.1F, 1.0F);

			if (pressed && hover) {
				bgcolor.w = 0.3F;
			} else if (hover) {
				bgcolor.w = 0.15F;
			}

			SetColorNP(r, bgcolor);
			r.DrawImage(nullptr, AABB2(pos.x + 1.0F, pos.y + 1.0F, sz.x, sz.y));

			// Checkbox.
			float boxSize = 14.0F;
			float boxX = pos.x + 5.0F;
			float boxY = pos.y + (sz.y - boxSize) * 0.5F;
			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.25F));
			r.DrawImage(nullptr, AABB2(boxX, boxY, boxSize, boxSize));
			if (enabled) {
				SetColorNP(r, fgcolor);
				r.DrawImage(nullptr,
				            AABB2(boxX + 3.0F, boxY + 3.0F, boxSize - 6.0F, boxSize - 6.0F));
			}

			// Apply-order number (own column, blank when disabled).
			float x = pos.x + checkColWidth;
			if (enabled && orderNum > 0)
				font->Draw(std::to_string(orderNum), MakeVector2(x + 2.0F, pos.y + 2.0F), 1.0F,
				           fgcolor);

			x = pos.x + checkColWidth + orderColWidth + 2.0F;
			font->Draw(modName, MakeVector2(x, pos.y + 2.0F), 1.0F, fgcolor);
			x = pos.x + checkColWidth + orderColWidth + nameColWidth + 2.0F;
			font->Draw(exists ? std::to_string(pakCount) : "-", MakeVector2(x, pos.y + 2.0F), 1.0F,
			           fgcolor);
			x += countColWidth;
			font->Draw(exists ? FormatFileSize(totalSize) : "-", MakeVector2(x, pos.y + 2.0F), 1.0F,
			           fgcolor);
		}

		// -- ModListModel --

		ModListModel::ModListModel(UIManager* manager, ModsScreenHelper* helper,
		                           std::vector<std::string> list, std::vector<int> orders,
		                           std::vector<bool> exists, float checkColWidth,
		                           float orderColWidth, float nameColWidth, float countColWidth,
		                           float sizeColWidth)
		    : manager(manager),
		      helper(helper),
		      list(std::move(list)),
		      orders(std::move(orders)),
		      exists(std::move(exists)),
		      checkColWidth(checkColWidth),
		      orderColWidth(orderColWidth),
		      nameColWidth(nameColWidth),
		      countColWidth(countColWidth),
		      sizeColWidth(sizeColWidth) {
			itemElements.resize(this->list.size());
		}

		void ModListModel::OnItemClicked(UIElement& sender) {
			ModListItem* item = dynamic_cast<ModListItem*>(&sender);
			if (item && itemActivated)
				itemActivated(item->modName);
		}

		Handle<UIElement> ModListModel::CreateElement(int row) {
			if (!itemElements[row]) {
				const std::string& name = list[row];
				bool ex = exists[row];
				int count = ex ? helper->GetModPakCount(name) : 0;
				std::int64_t size = ex ? helper->GetModTotalSize(name) : 0;
				Handle<ModListItem> item = Handle<ModListItem>::New(
				    manager, name, count, size, orders[row] > 0, ex, orders[row], checkColWidth,
				    orderColWidth, nameColWidth, countColWidth, sizeColWidth);
				item->activated = [this](UIElement& s) { OnItemClicked(s); };
				itemElements[row] = item;
			}
			return itemElements[row].Cast<UIElement>();
		}

		// -- ModListHeader --

		void ModListHeader::Render() {
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;
			font->Draw(text, pos + MakeVector2(2.0F, (sz.y - font->Measure(text).y) * 0.5F), 1.0F,
			           MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
		}

		// -- ModsProgressBar --

		void ModsProgressBar::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			float f = fraction;
			if (f < 0.0F)
				f = 0.0F;
			if (f > 1.0F)
				f = 1.0F;

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.12F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.55F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x * f, sz.y));
		}
	} // namespace gui
} // namespace spades
