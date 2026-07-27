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

#include "UIElement.h"
#include "Cursor.h"
#include "UIManager.h"
#include <Client/IFont.h>

namespace spades {
	namespace gui {
		namespace ui {
			UIElement::UIElement(UIManager* manager) : manager(manager) {}

			UIElement::~UIElement() {}

			void UIElement::SetParent(UIElement* value) {
				if (value == parent)
					return;
				if (parent)
					parent->RemoveChild(this);
				if (value)
					value->AddChild(this);
			}

			void UIElement::AppendChild(UIElement* element) {
				if (!element || element->parent == this)
					return;
				if (element->parent)
					element->parent->RemoveChild(element);
				element->parent = this;
				children.emplace_back(element);
			}

			void UIElement::PrependChild(UIElement* element) {
				if (!element || element->parent == this)
					return;
				if (element->parent)
					element->parent->RemoveChild(element);
				element->parent = this;
				children.emplace(children.begin(), element);
			}

			void UIElement::RemoveChild(UIElement* element) {
				if (!element || element->parent != this)
					return;

				// keep the child alive until we return, so handlers observing its
				// destruction run at a well-defined point
				Handle<UIElement> keepAlive(element);

				// drop any weak references the manager holds into this subtree before
				// the child (and possibly the whole subtree) is released
				manager->DiscardElement(element);

				element->parent = nullptr;
				for (auto it = children.begin(); it != children.end(); ++it) {
					if (it->GetPointerOrNull() == element) {
						children.erase(it);
						break;
					}
				}
			}

			client::IFont* UIElement::GetFont() const {
				if (fontOverride)
					return fontOverride.GetPointerOrNull();
				if (!parent)
					return nullptr;
				return parent->GetFont();
			}

			void UIElement::SetFont(client::IFont* value) { fontOverride = value; }

			Cursor* UIElement::GetCursor() const {
				if (cursorOverride)
					return cursorOverride.GetPointerOrNull();
				if (!parent)
					return manager->GetDefaultCursor();
				return parent->GetCursor();
			}

			void UIElement::SetCursor(Cursor* value) { cursorOverride = value; }

			bool UIElement::IsVisible() const {
				if (!visible)
					return false;
				if (!parent)
					return this == &manager->GetRootElement();
				return parent->IsVisible();
			}

			bool UIElement::IsEnabled() const {
				if (!enable)
					return false;
				if (!parent)
					return true;
				return parent->IsEnabled();
			}

			bool UIElement::IsFocused() const { return manager->GetActiveElement() == this; }

			Vector2 UIElement::GetScreenPosition() const {
				if (!parent)
					return position;
				return position + parent->GetScreenPosition();
			}

			void UIElement::SetBounds(const AABB2& value) {
				position = value.min;
				size = value.max - value.min;
				OnResized();
			}

			AABB2 UIElement::GetScreenBounds() const {
				Vector2 screenPos = GetScreenPosition();
				return AABB2(screenPos, screenPos + size);
			}

			int UIElement::GetTextEditingRangeStart() const {
				return IsFocused() ? manager->editingStart : 0;
			}

			int UIElement::GetTextEditingRangeLength() const {
				return IsFocused() ? manager->editingLength : 0;
			}

			std::string UIElement::GetEditingText() const {
				return IsFocused() ? manager->editingText : std::string();
			}

			UIElement* UIElement::MouseHitTest(Vector2 relativePos) {
				if (clipMouse && !GetBounds().Contains(relativePos))
					return nullptr;

				Vector2 p = relativePos - position;

				// front-to-back children are drawn in order, so the topmost element is
				// the last child; hit-test in reverse to prefer it
				for (auto it = children.rbegin(); it != children.rend(); ++it) {
					UIElement* e = it->GetPointerOrNull();
					if (!(e->visible && e->enable))
						continue;
					UIElement* hit = e->MouseHitTest(p);
					if (hit)
						return hit;
				}

				if (isMouseInteractive && GetBounds().Contains(relativePos))
					return this;

				return nullptr;
			}

			bool UIElement::IsSameOrDescendantOf(const UIElement* ancestor) const {
				for (const UIElement* e = this; e; e = e->parent) {
					if (e == ancestor)
						return true;
				}
				return false;
			}

			void UIElement::MouseWheel(float delta) {
				if (parent)
					parent->MouseWheel(delta);
			}

			void UIElement::MouseDown(MouseButton button, Vector2 clientPosition) {
				if (parent)
					parent->MouseDown(button, clientPosition);
			}

			void UIElement::MouseMove(Vector2 clientPosition) {
				if (parent)
					parent->MouseMove(clientPosition);
			}

			void UIElement::MouseUp(MouseButton button, Vector2 clientPosition) {
				if (parent)
					parent->MouseUp(button, clientPosition);
			}

			void UIElement::MouseEnter() {
				if (mouseEntered)
					mouseEntered(*this);
			}

			void UIElement::MouseLeave() {
				if (mouseLeft)
					mouseLeft(*this);
			}

			void UIElement::KeyDown(const std::string& key) { manager->ProcessHotKey(key); }

			void UIElement::KeyUp(const std::string&) {}

			void UIElement::KeyPress(const std::string&) {}

			void UIElement::HotKey(const std::string& key) {
				// A handler may mutate the tree, so iterate over a snapshot. Dispatch
				// is modal: if the child that handles the key detaches itself from us
				// (e.g. a dialog closing on Escape, which also re-enables its owner),
				// propagation stops there so the same key isn't delivered to a sibling
				// underneath in the same event. This mirrors the pre-C++ live-list
				// traversal, whose sibling chain broke at the removed node.
				std::vector<Handle<UIElement>> snapshot(children);
				for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) {
					UIElement* e = it->GetPointerOrNull();
					if (e->visible && e->enable) {
						e->HotKey(key);
						if (e->GetParent() != this)
							break;
					}
				}
			}

			void UIElement::Render() {
				std::vector<Handle<UIElement>> snapshot(children);
				for (Handle<UIElement>& child : snapshot) {
					if (child->visible)
						child->Render();
				}
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
