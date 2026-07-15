/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of OpenSpades.

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

#include <string>
#include <vector>

#include <Core/Math.h>
#include <Core/RefCountedObject.h>

#include "Event.h"

namespace spades {
	namespace client {
		class IFont;
	}
	namespace gui {
		namespace ui {
			class UIManager;
			class Cursor;

			/**
			 * Base class for every element in the UI tree.
			 *
			 * Ownership: a parent holds a strong reference (`Handle`) to each of its
			 * children; a child keeps only a weak (raw) back-pointer to its parent, so
			 * the tree forms no reference cycle. When an element is detached, the
			 * manager is told to drop any weak references it may hold to that subtree.
			 */
			class UIElement : public RefCountedObject {
				UIManager* manager;                      // weak; outlives the element
				UIElement* parent = nullptr;             // weak; the parent owns us
				std::vector<Handle<UIElement>> children; // strong, front-to-back order
				Handle<client::IFont> fontOverride;
				Handle<Cursor> cursorOverride;

			protected:
				virtual ~UIElement();

			public:
				EventHandler mouseEntered;
				EventHandler mouseLeft;

				bool visible = true;
				bool enable = true;

				/** When true, this element can receive keyboard focus on a mouse event. */
				bool acceptsFocus = false;

				/** When true, this element receives mouse events. */
				bool isMouseInteractive = false;

				/**
				 * When true, mouse interaction outside the client area is ignored for
				 * all sub-elements, reducing the hit-test cost. The visual is not clipped.
				 */
				bool clipMouse = true;

				Vector2 position;
				Vector2 size;

				UIElement(UIManager* manager);

				UIManager& GetManager() const { return *manager; }

				UIElement* GetParent() const { return parent; }
				void SetParent(UIElement* value);

				const std::vector<Handle<UIElement>>& GetChildren() const { return children; }

				void AppendChild(UIElement* element);
				void PrependChild(UIElement* element);
				void AddChild(UIElement* element) { AppendChild(element); }
				void RemoveChild(UIElement* element);

				client::IFont* GetFont() const;
				void SetFont(client::IFont* value);

				Cursor* GetCursor() const;
				void SetCursor(Cursor* value);

				bool IsVisible() const;
				bool IsEnabled() const;
				bool IsFocused() const;

				Vector2 GetScreenPosition() const;

				AABB2 GetBounds() const { return AABB2(position, position + size); }
				void SetBounds(const AABB2& value);

				AABB2 GetScreenBounds() const;

				// IME support

				virtual AABB2 GetTextInputRect() const { return AABB2(0, 0, size.x, size.y); }
				int GetTextEditingRangeStart() const;
				int GetTextEditingRangeLength() const;
				std::string GetEditingText() const;

				virtual void OnResized() {}

				/** Hit-tests this subtree. `relativePos` is parent-relative. */
				UIElement* MouseHitTest(Vector2 relativePos);

				Vector2 ScreenToClient(Vector2 scr) const { return scr - GetScreenPosition(); }

				/** Returns true if `this` is `ancestor` or a descendant of it. */
				bool IsSameOrDescendantOf(const UIElement* ancestor) const;

				virtual void MouseWheel(float delta);
				virtual void MouseDown(MouseButton button, Vector2 clientPosition);
				virtual void MouseMove(Vector2 clientPosition);
				virtual void MouseUp(MouseButton button, Vector2 clientPosition);
				virtual void MouseEnter();
				virtual void MouseLeave();

				virtual void KeyDown(const std::string& key);
				virtual void KeyUp(const std::string& key);
				virtual void KeyPress(const std::string& text);

				virtual void HotKey(const std::string& key);

				virtual void Render();
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
