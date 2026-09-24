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

#include "TextPromptScreen.h"

#include <algorithm>

#include "Button.h"
#include "DialogChrome.h"
#include "Field.h"
#include "Label.h"
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		using ui::Button;
		using ui::Field;
		using ui::Label;
		using ui::UIElement;
		using ui::UIManager;

		TextPromptScreen::TextPromptScreen(UIElement* owner, Options options)
		    : UIElement(&owner->GetManager()), owner(owner), validate(std::move(options.validate)) {
			SetFont(GetManager().GetRootElement().GetFont());
			SetBounds(owner->GetBounds());

			UIManager* manager = &GetManager();
			// Title, field, room for an error under it, then the button row: the
			// rhythm every prompt in the game is laid out on.
			contents = DialogChrome::Attach(*this, 500.0F, 160.0F);
			float x = contents.min.x;
			float y = contents.min.y;
			float w = contents.GetWidth();

			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->text = options.title;
				label->SetBounds(AABB2(x, y, w, 30.0F));
				label->alignment = MakeVector2(0.0F, 0.5F);
				AddChild(label.GetPointerOrNull());
			}
			{
				Handle<Field> f = Handle<Field>::New(manager);
				field = f.GetPointerOrNull();
				field->SetBounds(AABB2(x, y + 40.0F, w, 30.0F));
				field->placeholder = options.placeholder;
				field->SetText(options.initialText);
				size_t dot = options.initialText.find_last_of('.');
				if (options.selectStem && dot != std::string::npos && dot > 0)
					field->Select(0, static_cast<int>(dot));
				else
					field->SelectAll();
				// A stale error would describe text that no longer exists.
				field->changed = [this](UIElement&) { errorLabel->text.clear(); };
				AddChild(field);
			}
			{
				// Sits in the gap between the field and the buttons; empty (and so
				// invisible) until the validator rejects something.
				Handle<Label> label = Handle<Label>::New(manager);
				errorLabel = label.GetPointerOrNull();
				errorLabel->textColor = MakeVector4(1.0F, 0.45F, 0.4F, 1.0F);
				errorLabel->textScale = 0.75F;
				errorLabel->alignment = MakeVector2(0.0F, 0.5F);
				errorLabel->SetBounds(AABB2(x, y + 70.0F, w, 20.0F));
				AddChild(errorLabel);
			}
			{
				Handle<Button> btn = Handle<Button>::New(manager);
				btn->caption = options.confirmCaption.empty() ? _Tr("MessageBox", "OK")
				                                              : options.confirmCaption;
				btn->SetBounds(DialogChrome::ButtonSlot(contents, 1));
				btn->activated = [this](UIElement&) { OnConfirm(); };
				AddChild(btn.GetPointerOrNull());
			}
			{
				Handle<Button> btn = Handle<Button>::New(manager);
				btn->caption = _Tr("MessageBox", "Cancel");
				btn->SetBounds(DialogChrome::ButtonSlot(contents, 0));
				btn->activated = [this](UIElement&) { OnCancel(); };
				AddChild(btn.GetPointerOrNull());
			}
		}

		void TextPromptScreen::Render() {
			DialogChrome::DrawEdges(*this, contents);
			UIElement::Render();
		}

		void TextPromptScreen::OnConfirm() {
			if (validate) {
				std::string error = validate(field->GetText());
				if (!error.empty()) {
					errorLabel->text = error;
					GetManager().SetActiveElement(field);
					return;
				}
			}
			text = field->GetText();
			result = true;
			Close();
		}

		void TextPromptScreen::OnCancel() {
			result = false;
			Close();
		}

		void TextPromptScreen::Close() {
			// Removing ourselves drops the parent's reference, which may be the last
			// one: stay alive while the `closed` handlers run.
			Handle<TextPromptScreen> keepAlive(this);
			if (disabledOwner)
				owner->enable = true;
			GetParent()->RemoveChild(this);
			if (closed)
				closed(*this);
		}

		void TextPromptScreen::Run() {
			// The prompt sits next to its owner, which it disables while it is up.
			// An owner with no parent (a manager's root element) hosts the prompt
			// directly and must stay enabled: a disabled root swallows the mouse,
			// leaving the prompt unclickable.
			if (UIElement* parent = owner->GetParent()) {
				owner->enable = false;
				disabledOwner = true;
				parent->AddChild(this);
			} else {
				owner->AddChild(this);
			}
			// Typing is the whole point of a prompt, so it starts focused wherever
			// it was hosted.
			GetManager().SetActiveElement(field);
		}

		void TextPromptScreen::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Enter") {
				OnConfirm();
			} else if (IsEnabled() && key == "Escape") {
				OnCancel();
			} else {
				UIElement::HotKey(key);
			}
		}
	} // namespace gui
} // namespace spades
