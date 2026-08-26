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
#include "DropDownList.h"
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		namespace ui {
			void DropDownViewItem::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;

				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, hover ? 0.2F : 0.0F));
				r.DrawFilledRect(pos.x + 1, pos.y + 1, pos.x + sz.x - 1, pos.y + sz.y - 1);

				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, hover ? 0.3F : 0.0F));
				r.DrawOutlinedRect(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);

				client::IFont* font = GetFont();
				if (!font)
					return;
				Vector2 txtSize = font->Measure(caption);
				font->DrawShadow(caption,
				                 pos + (sz - txtSize) * MakeVector2(0.0F, 0.5F) +
				                     MakeVector2(4.0F, 0.0F),
				                 1.0F, MakeVector4(1, 1, 1, 1), MakeVector4(0, 0, 0, 0.4F));
			}

			// -- DropDownViewModel --

			DropDownViewModel::DropDownViewModel(UIManager* manager, std::vector<std::string> list)
			    : manager(manager), list(std::move(list)) {}

			void DropDownViewModel::ItemClicked(UIElement& sender) {
				DropDownViewItem* item = dynamic_cast<DropDownViewItem*>(&sender);
				if (item && handler)
					handler(item->index);
			}

			Handle<UIElement> DropDownViewModel::CreateElement(int row) {
				Handle<DropDownViewItem> i = Handle<DropDownViewItem>::New(manager);
				i->caption = list[row];
				i->index = row;
				i->activated = [this](UIElement& sender) { ItemClicked(sender); };
				return std::move(i).Cast<UIElement>();
			}

			// -- DropDownBackground --

			DropDownBackground::DropDownBackground(UIManager* manager, DropDownView* view)
			    : UIElement(manager), view(view) {
				isMouseInteractive = true;
			}

			void DropDownBackground::Destroy() {
				// keep alive while detaching: we own the view, and our parent owns us
				Handle<DropDownBackground> keepAlive(this);
				SetParent(nullptr);
				if (oldRoot)
					oldRoot->enable = true;
			}

			void DropDownBackground::MouseDown(MouseButton button, Vector2 clientPosition) {
				(void)button;
				(void)clientPosition;
				DropDownListHandler handler = view->handler;
				view->Destroy();
				if (handler)
					handler(-1);
			}

			void DropDownBackground::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.5F));
				r.DrawImage(nullptr, GetScreenBounds());
				UIElement::Render();
			}

			// -- DropDownView --

			DropDownView::DropDownView(UIManager* manager, DropDownListHandler handler,
			                           std::vector<std::string> items)
			    : ListView(manager), handler(std::move(handler)) {
				Handle<DropDownViewModel> model =
				    Handle<DropDownViewModel>::New(manager, std::move(items));
				model->handler = [this](int index) { ItemActivated(index); };
				SetModel(model.GetPointerOrNull());
			}

			void DropDownView::Destroy() { bg->Destroy(); }

			void DropDownView::ItemActivated(int index) {
				DropDownListHandler h = handler;
				Destroy();
				if (h)
					h(index);
			}

			// -- ShowDropDownList --

			void ShowDropDownList(UIManager* manager, float x, float y, float width,
			                      std::vector<std::string> items, DropDownListHandler handler) {
				Handle<DropDownView> view =
				    Handle<DropDownView>::New(manager, std::move(handler), items);
				Handle<DropDownBackground> bg =
				    Handle<DropDownBackground>::New(manager, view.GetPointerOrNull());
				view->bg = bg.GetPointerOrNull();

				UIElement& root = manager->GetRootElement();
				Vector2 sz = root.size;

				float maxHeight = sz.y - y;
				float height = 24.0F * static_cast<float>(items.size());
				if (height > maxHeight)
					height = maxHeight - 20.0F;

				bg->SetBounds(AABB2(0.0F, 0.0F, sz.x, sz.y));
				view->SetBounds(AABB2(x, y, width, height));

				root.AddChild(bg.GetPointerOrNull());
				bg->AddChild(view.GetPointerOrNull());
			}

			void ShowDropDownList(UIElement& e, std::vector<std::string> items,
			                      DropDownListHandler handler) {
				AABB2 b = e.GetScreenBounds();
				ShowDropDownList(&e.GetManager(), b.min.x, b.max.y, b.max.x - b.min.x,
				                 std::move(items), std::move(handler));
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
