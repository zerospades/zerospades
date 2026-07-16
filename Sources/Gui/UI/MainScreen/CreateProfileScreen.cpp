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

#include "CreateProfileScreen.h"
#include <Client/Fonts.h>
#include <Client/IRenderer.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/Label.h>

SPADES_SETTING(cg_playerName);
SPADES_SETTING(cg_playerNameIsSet);

namespace spades {
	namespace gui {
		using ui::Button;
		using ui::Field;
		using ui::Label;
		using ui::SetColorNP;
		using ui::UIElement;
		using ui::UIManager;

		CreateProfileScreen::CreateProfileScreen(UIElement* owner,
		                                         client::FontManager* fontManager)
		    : UIElement(&owner->GetManager()), owner(owner) {
			SetBounds(owner->GetBounds());

			UIManager* manager = &GetManager();
			float contentsWidth = 500.0F;
			float contentsLeft = (manager->screenWidth - contentsWidth) * 0.5F;
			contentsHeight = 188.0F;
			contentsTop = (manager->screenHeight - contentsHeight) * 0.5F;

			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.9F);
				label->SetBounds(AABB2(0.0F, contentsTop - 13.0F, size.x, contentsHeight + 27.0F));
				AddChild(label.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("CreateProfileScreen", "OK");
				button->SetBounds(AABB2(contentsLeft + contentsWidth - 140.0F,
				                        contentsTop + contentsHeight - 40.0F, 140.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnOkPressed(s); };
				button->enable = false;
				AddChild(button.GetPointerOrNull());
				okButton = button.GetPointerOrNull();
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("CreateProfileScreen", "Decide later");
				button->SetBounds(
				    AABB2(contentsLeft, contentsTop + contentsHeight - 40.0F, 140.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnChooseLater(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->text = _Tr("CreateProfileScreen", "Welcome to ZeroSpades");
				label->SetFont(&fontManager->GetHeadingFont());
				label->SetBounds(AABB2(contentsLeft, contentsTop + 10.0F, contentsWidth, 32.0F));
				label->alignment = MakeVector2(0.0F, 0.5F);
				AddChild(label.GetPointerOrNull());
			}
			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->text = _Tr("CreateProfileScreen", "Choose a player name:");
				label->SetBounds(AABB2(contentsLeft, contentsTop + 42.0F, contentsWidth, 32.0F));
				label->alignment = MakeVector2(0.0F, 0.5F);
				AddChild(label.GetPointerOrNull());
			}
			{
				Handle<Field> field = Handle<Field>::New(manager);
				nameField = field.GetPointerOrNull();
				nameField->SetBounds(AABB2(contentsLeft, contentsTop + 74.0F, contentsWidth, 30.0F));
				nameField->placeholder = _Tr("CreateProfileScreen", "Player name");
				nameField->changed = [this](UIElement& s) { OnNameChanged(s); };
				nameField->maxLength = 15;
				nameField->denyNonAscii = false;
				nameField->removeNewlines = true;
				AddChild(nameField);
			}
			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->text =
				    _Tr("CreateProfileScreen", "You can change it later in the Setup dialog.");
				label->SetBounds(AABB2(contentsLeft, contentsTop + 106.0F, contentsWidth, 32.0F));
				label->alignment = MakeVector2(0.0F, 0.5F);
				AddChild(label.GetPointerOrNull());
			}
		}

		void CreateProfileScreen::Close() {
			owner->enable = true;
			GetParent()->RemoveChild(this);
		}

		void CreateProfileScreen::Run() {
			owner->enable = false;
			owner->GetParent()->AddChild(this);

			GetManager().SetActiveElement(nameField);
		}

		void CreateProfileScreen::OnOkPressed(UIElement&) {
			if (nameField->GetText().empty())
				return;
			cg_playerName = nameField->GetText();
			cg_playerNameIsSet = 1;
			Close();
		}

		void CreateProfileScreen::OnChooseLater(UIElement&) {
			cg_playerName = std::string("Deuce");
			Close();
		}

		void CreateProfileScreen::OnNameChanged(UIElement&) {
			okButton->enable = nameField->GetText().size() > 0;
		}

		void CreateProfileScreen::HotKey(const std::string& key) {
			if (IsEnabled() && (key == "Enter" || key == "Escape")) {
				if (key == "Enter") {
					OnOkPressed(*this);
				} else if (key == "Escape") {
					OnChooseLater(*this);
				}
			} else {
				UIElement::HotKey(key);
			}
		}

		void CreateProfileScreen::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.08F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y + contentsTop - 15.0F, sz.x, 1.0F));
			r.DrawImage(nullptr,
			            AABB2(pos.x, pos.y + contentsTop + contentsHeight + 15.0F, sz.x, 1.0F));
			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.2F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y + contentsTop - 14.0F, sz.x, 1.0F));
			r.DrawImage(nullptr,
			            AABB2(pos.x, pos.y + contentsTop + contentsHeight + 14.0F, sz.x, 1.0F));

			UIElement::Render();
		}
	} // namespace gui
} // namespace spades
