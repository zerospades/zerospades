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

#include "DrawUtils.h"
#include "TabStrip.h"
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		namespace ui {
			SimpleTabStripItem::SimpleTabStripItem(UIManager* manager, UIElement* linkedElement)
			    : ButtonBase(manager), linkedElement(linkedElement) {}

			void SimpleTabStripItem::MouseDown(MouseButton button, Vector2 clientPosition) {
				(void)button;
				(void)clientPosition;
				PlayActivateSound();
				OnActivated();
			}

			void SimpleTabStripItem::Render() {
				toggled = linkedElement->visible;

				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;
				Vector4 textColor = MakeVector4(0.9F, 0.9F, 0.9F, 1.0F);

				if (toggled) {
					SetColorNP(r, MakeVector4(0.9F, 0.9F, 0.9F, 1.0F));
					textColor = MakeVector4(0.0F, 0.0F, 0.0F, 1.0F);
				} else if (hover) {
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.3F));
				} else {
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.0F));
				}
				r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));

				client::IFont* font = GetFont();
				if (!font)
					return;
				Vector2 txtSize = font->Measure(caption);
				font->Draw(caption, pos + (sz - txtSize) * 0.5F, 1.0F, textColor);
			}

			// -- SimpleTabStrip --

			void SimpleTabStrip::OnItemActivated(UIElement& sender) {
				SimpleTabStripItem* item = dynamic_cast<SimpleTabStripItem*>(&sender);
				if (!item)
					return;
				UIElement* linked = item->linkedElement;
				for (const Handle<UIElement>& child : GetChildren()) {
					SimpleTabStripItem* otherItem =
					    dynamic_cast<SimpleTabStripItem*>(child.GetPointerOrNull());
					if (otherItem)
						otherItem->linkedElement->visible = (otherItem->linkedElement == linked);
				}
				if (changed)
					changed(*this);
			}

			void SimpleTabStrip::AddItem(const std::string& title, UIElement* linkedElement) {
				Handle<SimpleTabStripItem> item =
				    Handle<SimpleTabStripItem>::New(&GetManager(), linkedElement);
				item->caption = title;
				client::IFont* font = GetFont();
				float w = (font ? font->Measure(title).x : 0.0F) + 18.0F;
				item->SetBounds(AABB2(nextX, 0.0F, w, 24.0F));
				nextX += w + 4.0F;

				item->activated = [this](UIElement& sender) { OnItemActivated(sender); };

				AddChild(item.GetPointerOrNull());
			}

			void SimpleTabStrip::Render() {
				UIElement::Render();

				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;

				SetColorNP(r, MakeVector4(0.9F, 0.9F, 0.9F, 1.0F));
				r.DrawImage(nullptr, AABB2(pos.x, pos.y + 24.0F, sz.x, 1.0F));
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
