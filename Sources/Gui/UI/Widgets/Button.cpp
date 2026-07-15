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

#include "Button.h"
#include "DrawUtils.h"
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		namespace ui {
			void Button::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;

				Vector4 color = MakeVector4(0.2F, 0.2F, 0.2F, 0.5F);

				if (IsEnabled()) {
					if (toggled || (pressed && hover))
						color = MakeVector4(0.7F, 0.7F, 0.7F, 0.9F);
					else if (hover)
						color = MakeVector4(0.4F, 0.4F, 0.4F, 0.7F);
				} else {
					color.w *= 0.5F;
				}

				SetColorNP(r, color);
				r.DrawFilledRect(pos.x + 1, pos.y + 1, pos.x + sz.x - 1, pos.y + sz.y - 1);

				SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, color.w));
				r.DrawOutlinedRect(pos.x + 1, pos.y + 1, pos.x + sz.x - 1, pos.y + sz.y - 1);

				client::IFont* font = GetFont();
				if (!font)
					return;

				pos += MakeVector2(8.0F, 8.0F);
				sz -= MakeVector2(16.0F, 16.0F);

				if (hotKeyText.size() > 0)
					alignment = MakeVector2(0.0F, 0.5F);

				Vector2 txtSize = font->Measure(caption);
				Vector2 txtPos = pos + (sz - txtSize) * alignment;
				font->DrawShadow(caption, txtPos, 1.0F,
				                 MakeVector4(1.0F, 1.0F, 1.0F, IsEnabled() ? 1.0F : 0.5F),
				                 MakeVector4(0.0F, 0.0F, 0.0F, IsEnabled() ? 0.4F : 0.1F));

				// TODO: use "FontManager::SmallFont" for this
				txtSize = font->Measure(hotKeyText);
				txtPos = pos + (sz - txtSize) * hotKeyTextAlignment;
				font->DrawShadow(hotKeyText, txtPos, 1.0F,
				                 MakeVector4(1.0F, 1.0F, 1.0F, IsEnabled() ? 0.6F : 0.3F),
				                 MakeVector4(0.0F, 0.0F, 0.0F, IsEnabled() ? 0.1F : 0.05F));
			}

			void SimpleButton::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;

				Vector4 color = IsEnabled() ? textColor : disabledTextColor;

				if (toggled || (pressed && hover))
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.2F));
				else if (hover)
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.12F));
				else
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F * color.w));
				r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));

				if (toggled || (pressed && hover))
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.1F));
				else if (hover)
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));
				else
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.03F * color.w));
				r.DrawOutlinedRect(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);

				client::IFont* font = GetFont();
				if (!font)
					return;

				pos += MakeVector2(4.0F, 4.0F);
				sz -= MakeVector2(8.0F, 8.0F);

				Vector2 txtSize = font->Measure(caption);
				Vector2 txtPos = pos + (sz - txtSize) * alignment;

				font->DrawShadow(caption, txtPos, 1.0F, color,
				                 MakeVector4(0.0F, 0.0F, 0.0F, 0.4F * color.w));
			}

			void RadioButton::Check() {
				toggled = true;

				// uncheck others
				if (groupName.size() > 0 && GetParent()) {
					const std::vector<Handle<UIElement>>& children = GetParent()->GetChildren();
					for (const Handle<UIElement>& child : children) {
						RadioButton* btn = dynamic_cast<RadioButton*>(child.GetPointerOrNull());
						if (btn == this)
							continue;
						if (btn) {
							if (groupName == btn->groupName)
								btn->toggled = false;
						}
					}
				}
			}

			void RadioButton::OnActivated() {
				Check();
				Button::OnActivated();
			}

			void RadioButton::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;

				if (!enable)
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));

				if (toggled || (pressed && hover))
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.2F));
				else if (hover)
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.12F));
				else
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));
				r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));

				if (!enable)
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.03F));

				if (toggled || (pressed && hover))
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.06F));
				else if (hover)
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.04F));
				else
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.02F));
				r.DrawOutlinedRect(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);

				client::IFont* font = GetFont();
				if (!font)
					return;

				Vector2 txtSize = font->Measure(caption);
				font->DrawShadow(caption, pos + (sz - txtSize) * 0.5F + MakeVector2(8.0F, 0.0F),
				                 1.0F,
				                 MakeVector4(1, 1, 1, (toggled && enable) ? 1.0F : 0.4F),
				                 MakeVector4(0, 0, 0, 0.4F));

				if (toggled) {
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, (toggled && enable) ? 0.6F : 0.3F));
					r.DrawImage(nullptr,
					            AABB2(pos.x + 4.0F, pos.y + (sz.y - 8.0F) * 0.5F, 8.0F, 8.0F));
				}
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
