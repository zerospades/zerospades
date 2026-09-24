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
#include "DialogChrome.h"
#include "MessageBox.h"
#include "TextViewer.h"
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		using ui::Button;
		using ui::TextViewer;
		using ui::UIElement;

		MessageBoxScreen::MessageBoxScreen(UIElement* owner, const std::string& text,
		                                   const std::vector<std::string>& buttons, float height,
		                                   bool showOverlay)
		    : UIElement(&owner->GetManager()), owner(owner) {
			SetFont(GetManager().GetRootElement().GetFont());
			SetBounds(owner->GetBounds());

			contents = DialogChrome::Attach(*this, 800.0F, height, showOverlay);

			for (size_t i = 0; i < buttons.size(); ++i) {
				Handle<Button> button = Handle<Button>::New(&GetManager());
				button->caption = buttons[i];
				// The last button is the rightmost, so the row reads in the order
				// it was given.
				button->SetBounds(
				  DialogChrome::ButtonSlot(contents, int(buttons.size() - 1 - i)));

				int resultIdx = static_cast<int>(i);
				button->activated = [this, resultIdx](UIElement&) { EndDialog(resultIdx); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<TextViewer> viewer = Handle<TextViewer>::New(&GetManager());
				AddChild(viewer.GetPointerOrNull());
				viewer->SetBounds(AABB2(contents.min.x, contents.min.y, contents.GetWidth(),
				                        contents.GetHeight() - 40.0F));
				viewer->SetText(text);
			}
		}

		void MessageBoxScreen::OnClosed() {
			if (closed)
				closed(*this);
		}

		void MessageBoxScreen::EndDialog(int result) {
			resultIndex = result;
			Close();
		}

		void MessageBoxScreen::Close() {
			// RemoveChild drops the parent's strong reference — which is the only
			// one left once Run()'s caller-side handle is gone — so hold the
			// dialog alive across OnClosed(); otherwise the `closed` callback runs
			// on a freed object and is silently lost (e.g. the mod download never
			// starts after pressing OK).
			Handle<MessageBoxScreen> keepAlive(this);
			owner->enable = true;
			GetParent()->RemoveChild(this);
			OnClosed();
		}

		void MessageBoxScreen::Run() {
			owner->enable = false;
			owner->GetParent()->AddChild(this);
		}

		void MessageBoxScreen::Render() {
			DialogChrome::DrawEdges(*this, contents);
			UIElement::Render();
		}

		// -- AlertScreen --

		AlertScreen::AlertScreen(UIElement* owner, const std::string& text, float height,
		                         bool showOverlay)
		    : MessageBoxScreen(owner, text, {_Tr("MessageBox", "OK")}, height, showOverlay) {}

		void AlertScreen::HotKey(const std::string& key) {
			if (IsEnabled() && (key == "Enter" || key == "Escape")) {
				EndDialog(0);
			} else {
				UIElement::HotKey(key);
			}
		}

		// -- ConfirmScreen --

		ConfirmScreen::ConfirmScreen(UIElement* owner, const std::string& text, float height,
		                             bool showOverlay)
		    : MessageBoxScreen(owner, text,
		                       {_Tr("MessageBox", "OK"), _Tr("MessageBox", "Cancel")}, height,
		                       showOverlay) {}

		void ConfirmScreen::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Enter") {
				EndDialog(0);
			} else if (IsEnabled() && key == "Escape") {
				EndDialog(1);
			} else {
				UIElement::HotKey(key);
			}
		}
	} // namespace gui
} // namespace spades
