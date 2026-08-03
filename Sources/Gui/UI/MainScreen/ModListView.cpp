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
#include <Client/IImage.h>
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

		namespace {
			// Maps an official CATEGORY tag to a weapon icon in the game
			// resources. Returns "" for categories with no weapon icon (FONT,
			// SFX, VFX, ...), which are shown as a text pill instead. Matching is
			// against the uppercase tags used by the official repo.
			std::string ModCategoryIcon(const std::string& category) {
				if (category == "SEMI")
					return "Gfx/Hotbar/Rifle.png";
				if (category == "SMG")
					return "Gfx/Hotbar/SMG.png";
				if (category == "SHOTGUN")
					return "Gfx/Hotbar/Shotgun.png";
				if (category == "SPADE")
					return "Gfx/Hotbar/Spade.png";
				if (category == "GRENADE")
					return "Gfx/Hotbar/Grenade.png";
				return std::string{};
			}
		} // namespace

		// -- ModListItem --

		ModListItem::ModListItem(UIManager* manager, const std::string& modName,
		                         const std::string& category, const std::string& displayName,
		                         const std::string& author, std::int64_t totalSize, bool enabled,
		                         bool exists, int orderNum, float checkColWidth, float orderColWidth,
		                         float tagColWidth, float nameColWidth, float authorColWidth,
		                         float sizeColWidth)
		    : ui::ButtonBase(manager),
		      category(category),
		      displayName(displayName),
		      author(author),
		      totalSize(totalSize),
		      enabled(enabled),
		      exists(exists),
		      orderNum(orderNum),
		      checkColWidth(checkColWidth),
		      orderColWidth(orderColWidth),
		      tagColWidth(tagColWidth),
		      nameColWidth(nameColWidth),
		      authorColWidth(authorColWidth),
		      modName(modName) {
			(void)sizeColWidth;
		}

		void ModListItem::RenderTag(client::IRenderer& r, client::IFont* font, float cellX,
		                            float cellY, float cellH, const Vector4& fgcolor) {
			if (category.empty())
				return;
			std::string iconPath = ModCategoryIcon(category);
			if (!iconPath.empty()) {
				Handle<client::IImage> icon = r.RegisterImage(iconPath.c_str());
				// Fit within the cell height, preserving aspect ratio.
				float h = cellH - 8.0F;
				float w = h * (icon->GetWidth() / icon->GetHeight());
				if (w > tagColWidth - 6.0F) {
					w = tagColWidth - 6.0F;
					h = w * (icon->GetHeight() / icon->GetWidth());
				}
				float ix = cellX + (tagColWidth - w) * 0.5F;
				float iy = cellY + (cellH - h) * 0.5F;
				SetColorNP(r, fgcolor);
				r.DrawImage(icon, AABB2(ix, iy, w, h));
			} else if (font) {
				// Non-weapon category: draw the tag text, centered in the cell.
				float textW = font->Measure(category).x;
				float tx = cellX + (tagColWidth - textW) * 0.5F;
				font->Draw(category, MakeVector2(tx, cellY + 2.0F), 1.0F, fgcolor);
			}
		}

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

			// Tag column (weapon icon or category pill).
			x = pos.x + checkColWidth + orderColWidth;
			RenderTag(r, font, x, pos.y, sz.y, fgcolor);

			// Mod name. For an unstructured name this holds the full filename and
			// runs into the author column, matching the previous behaviour.
			x = pos.x + checkColWidth + orderColWidth + tagColWidth + 2.0F;
			font->Draw(displayName, MakeVector2(x, pos.y + 2.0F), 1.0F, fgcolor);

			// Author.
			x += nameColWidth;
			if (!author.empty())
				font->Draw(author, MakeVector2(x, pos.y + 2.0F), 1.0F, fgcolor);

			// Size.
			x += authorColWidth;
			font->Draw(exists ? FormatFileSize(totalSize) : "-", MakeVector2(x, pos.y + 2.0F), 1.0F,
			           fgcolor);
		}

		// -- ModListModel --

		ModListModel::ModListModel(UIManager* manager, ModsScreenHelper* helper,
		                           std::vector<std::string> list, std::vector<int> orders,
		                           std::vector<bool> exists, float checkColWidth,
		                           float orderColWidth, float tagColWidth, float nameColWidth,
		                           float authorColWidth, float sizeColWidth)
		    : manager(manager),
		      helper(helper),
		      list(std::move(list)),
		      orders(std::move(orders)),
		      exists(std::move(exists)),
		      checkColWidth(checkColWidth),
		      orderColWidth(orderColWidth),
		      tagColWidth(tagColWidth),
		      nameColWidth(nameColWidth),
		      authorColWidth(authorColWidth),
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
				std::int64_t size = ex ? helper->GetModTotalSize(name) : 0;
				std::string category = helper->GetModCategory(name);
				std::string displayName = helper->GetModDisplayName(name);
				std::string author = helper->GetModAuthor(name);
				Handle<ModListItem> item = Handle<ModListItem>::New(
				    manager, name, category, displayName, author, size, orders[row] > 0, ex,
				    orders[row], checkColWidth, orderColWidth, tagColWidth, nameColWidth,
				    authorColWidth, sizeColWidth);
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
