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
#include "Label.h"
#include "MessageBox.h"
#include "TextViewer.h"
#include <Client/IRenderer.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		using ui::Button;
		using ui::Label;
		using ui::SetColorNP;
		using ui::TextViewer;
		using ui::UIElement;

		MessageBoxScreen::MessageBoxScreen(UIElement* owner, const std::string& text,
		                                   const std::vector<std::string>& buttons, float height)
		    : UIElement(&owner->GetManager()), owner(owner) {
			SetFont(GetManager().GetRootElement().GetFont());
			SetBounds(owner->GetBounds());

			float sw = GetManager().screenWidth;
			float sh = GetManager().screenHeight;

			float contentsWidth = sw - 16.0F;
			float maxContentsWidth = 800.0F;
			if (contentsWidth > maxContentsWidth)
				contentsWidth = maxContentsWidth;

			float contentsLeft = (sw - contentsWidth) * 0.5F;
			contentsHeight = height;
			contentsTop = (sh - contentsHeight) * 0.5F;

			{
				Handle<Label> label = Handle<Label>::New(&GetManager());
				label->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.9F);
				label->SetBounds(AABB2(0.0F, contentsTop - 13.0F, size.x, contentsHeight + 27.0F));
				AddChild(label.GetPointerOrNull());
			}

			for (size_t i = 0; i < buttons.size(); ++i) {
				Handle<Button> button = Handle<Button>::New(&GetManager());
				button->caption = buttons[i];
				button->SetBounds(
				    AABB2(contentsLeft + contentsWidth - (150.0F + 10.0F) * (buttons.size() - i) +
				              10.0F,
				          contentsTop + contentsHeight - 30.0F, 150.0F, 30.0F));

				int resultIdx = static_cast<int>(i);
				button->activated = [this, resultIdx](UIElement&) { EndDialog(resultIdx); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<TextViewer> viewer = Handle<TextViewer>::New(&GetManager());
				AddChild(viewer.GetPointerOrNull());
				viewer->SetBounds(
				    AABB2(contentsLeft, contentsTop, contentsWidth, contentsHeight - 40.0F));
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
			owner->enable = true;
			GetParent()->RemoveChild(this);
			OnClosed();
		}

		void MessageBoxScreen::Run() {
			owner->enable = false;
			owner->GetParent()->AddChild(this);
		}

		void MessageBoxScreen::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y + contentsTop - 14.0F, sz.x, 1.0F));
			r.DrawImage(nullptr,
			            AABB2(pos.x, pos.y + contentsTop + contentsHeight + 14.0F, sz.x, 1.0F));

			UIElement::Render();
		}

		// -- AlertScreen --

		AlertScreen::AlertScreen(UIElement* owner, const std::string& text, float height)
		    : MessageBoxScreen(owner, text, {_Tr("MessageBox", "OK")}, height) {}

		void AlertScreen::HotKey(const std::string& key) {
			if (IsEnabled() && (key == "Enter" || key == "Escape")) {
				EndDialog(0);
			} else {
				UIElement::HotKey(key);
			}
		}

		// -- ConfirmScreen --

		ConfirmScreen::ConfirmScreen(UIElement* owner, const std::string& text, float height)
		    : MessageBoxScreen(owner, text,
		                       {_Tr("MessageBox", "OK"), _Tr("MessageBox", "Cancel")}, height) {}

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
