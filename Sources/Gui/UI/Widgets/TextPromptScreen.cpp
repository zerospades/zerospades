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
			float sw = manager->screenWidth;
			float sh = manager->screenHeight;
			float w = std::min(sw - 16.0F, 500.0F);
			float h = 160.0F;
			float x = (sw - w) * 0.5F;
			float y = (sh - h) * 0.5F;

			// Dims the screen behind the prompt, as the message boxes do.
			{
				Handle<Label> overlay = Handle<Label>::New(manager);
				overlay->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.7F);
				overlay->SetBounds(AABB2(0.0F, 0.0F, sw, sh));
				AddChild(overlay.GetPointerOrNull());
			}
			{
				Handle<Label> bg = Handle<Label>::New(manager);
				bg->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.9F);
				bg->SetBounds(AABB2(0.0F, y - 13.0F, size.x, h + 27.0F));
				AddChild(bg.GetPointerOrNull());
			}
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
				btn->SetBounds(AABB2(x + w - 310.0F, y + 130.0F, 150.0F, 30.0F));
				btn->activated = [this](UIElement&) { OnConfirm(); };
				AddChild(btn.GetPointerOrNull());
			}
			{
				Handle<Button> btn = Handle<Button>::New(manager);
				btn->caption = _Tr("MessageBox", "Cancel");
				btn->SetBounds(AABB2(x + w - 150.0F, y + 130.0F, 150.0F, 30.0F));
				btn->activated = [this](UIElement&) { OnCancel(); };
				AddChild(btn.GetPointerOrNull());
			}
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
			owner->enable = true;
			GetParent()->RemoveChild(this);
			if (closed)
				closed(*this);
		}

		void TextPromptScreen::Run() {
			owner->enable = false;
			owner->GetParent()->AddChild(this);
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
