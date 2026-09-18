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

#include "UIOverlayHost.h"
#include "SoftwareCursor.h"

#include <Client/IRenderer.h>

namespace spades {
	namespace gui {
		using ui::UIElement;

		UIOverlayHost::UIOverlayHost(client::IRenderer& renderer, client::IAudioDevice* audioDevice,
		                             client::FontManager& fontManager, SoftwareCursor& cursor)
		    : manager(Handle<ui::UIManager>::New(&renderer, audioDevice)), fontManager(&fontManager),
		      cursor(cursor) {
			manager->GetRootElement().SetFont(&this->fontManager->GetGuiFont());
		}

		UIOverlayHost::~UIOverlayHost() {}

		void UIOverlayHost::Show(UIElement* dialog) {
			if (!dialog)
				return;
			UIElement& root = manager->GetRootElement();
			dialog->SetBounds(root.GetBounds());
			// The pointer starts where the software cursor already is, so the first
			// hover is correct before any mouse motion arrives.
			manager->MouseEvent(cursor.GetPosition().x, cursor.GetPosition().y);
			if (!dialog->GetParent()) // a dialog may attach itself in its own Run()
				root.AddChild(dialog);
		}

		bool UIOverlayHost::IsActive() const {
			return !manager->GetRootElement().GetChildren().empty();
		}

		void UIOverlayHost::Close() {
			UIElement& root = manager->GetRootElement();
			while (!root.GetChildren().empty())
				root.RemoveChild(root.GetChildren().front().GetPointerOrNull());
			manager->KeyPanic();
		}

		bool UIOverlayHost::KeyEvent(const std::string& key, bool down) {
			if (!IsActive())
				return false;
			manager->KeyEvent(key, down);
			// A dialog may have closed on this key; drop any key repeat it left.
			if (!IsActive())
				manager->KeyPanic();
			return true;
		}

		void UIOverlayHost::TextInputEvent(const std::string& text) {
			if (IsActive())
				manager->TextInputEvent(text);
		}

		void UIOverlayHost::TextEditingEvent(const std::string& text, int start, int len) {
			if (IsActive())
				manager->TextEditingEvent(text, start, len);
		}

		bool UIOverlayHost::AcceptsTextInput() const {
			return IsActive() && manager->AcceptsTextInput();
		}

		AABB2 UIOverlayHost::GetTextInputRect() const {
			return IsActive() ? manager->GetTextInputRect() : AABB2();
		}

		bool UIOverlayHost::WheelEvent(float x, float y) {
			if (!IsActive())
				return false;
			manager->WheelEvent(x, y);
			return true;
		}

		void UIOverlayHost::RunFrame(float dt) {
			if (!IsActive())
				return;
			// The view drives the software cursor from relative motion; the widgets
			// expect absolute coordinates, so hand them the cursor's position.
			manager->MouseEvent(cursor.GetPosition().x, cursor.GetPosition().y);
			manager->RunFrame(dt);
		}

		void UIOverlayHost::Draw() {
			if (!IsActive())
				return;
			manager->Render();
		}
	} // namespace gui
} // namespace spades
