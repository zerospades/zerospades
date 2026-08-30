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

#include "ClientMenu.h"
#include <Client/ClientUI.h>
#include <Client/ClientUIHelper.h>
#include <Client/Fonts.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Label.h>

namespace spades {
	namespace client {
		using gui::PreferenceView;
		using gui::PreferenceViewOptions;
		using gui::PreferenceViewPersistedState;
		using gui::ui::Button;
		using gui::ui::Label;
		using gui::ui::UIElement;
		using gui::ui::UIManager;

		ClientMenu::ClientMenu(ClientUI* ui)
		    : UIElement(&ui->GetUIManager()), ui(ui), helper(&ui->GetHelper()) {
			preferenceState = Handle<PreferenceViewPersistedState>::New();

			UIManager* manager = &GetManager();
			float sw = manager->screenWidth;
			float sh = manager->screenHeight;

			float winW = 180.0F, winH = 32.0F * 4.0F - 2.0F;
			float winX = (sw - winW) * 0.5F;
			float winY = (sh - winH) * 0.5F;

			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.5F);
				label->SetBounds(AABB2(winX - 8.0F, winY - 8.0F, winW + 16.0F, winH + 16.0F));
				AddChild(label.GetPointerOrNull());
			}

			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Back to Game");
				button->SetBounds(AABB2(winX, winY, winW, 30.0F));
				button->activated = [this](UIElement& s) { OnBackToGame(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Chat Log");
				button->SetBounds(AABB2(winX, winY + 32.0F, winW, 30.0F));
				button->activated = [this](UIElement& s) { OnChatLog(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Setup");
				button->SetBounds(AABB2(winX, winY + 64.0F, winW, 30.0F));
				button->activated = [this](UIElement& s) { OnSetup(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = helper->IsDemoMode() ? _Tr("Client", "Exit Demo")
				                                       : _Tr("Client", "Disconnect");
				button->SetBounds(AABB2(winX, winY + 96.0F, winW, 30.0F));
				button->activated = [this](UIElement& s) { OnDisconnect(s); };
				AddChild(button.GetPointerOrNull());
			}
		}

		void ClientMenu::OnBackToGame(UIElement&) { ui->SetActiveUI(nullptr); }

		void ClientMenu::OnSetup(UIElement&) {
			PreferenceViewOptions opt;
			opt.gameActive = true;
			opt.persistedState = preferenceState;

			Handle<PreferenceView> al =
			    Handle<PreferenceView>::New(this, opt, &ui->GetFontManager());
			al->Run();
		}

		void ClientMenu::OnChatLog(UIElement&) { ui->EnterChatLogWindow(); }

		void ClientMenu::OnDisconnect(UIElement&) { ui->shouldExit = true; }

		void ClientMenu::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Escape") {
				ui->SetActiveUI(nullptr);
			} else {
				UIElement::HotKey(key);
			}
		}

		// IModalMenu implementation
		bool ClientMenu::IsActive() const { return IsEnabled(); }
		void ClientMenu::Open() { /* Managed by ui->SetActiveUI(this) */ }
		void ClientMenu::Close() { if (ui) ui->SetActiveUI(nullptr); }
		bool ClientMenu::KeyEvent(const std::string& key, bool down) {
			if (down) KeyDown(key);
			else KeyUp(key);
			return true;
		}
		void ClientMenu::TextInputEvent(const std::string& text) { KeyPress(text); }
		bool ClientMenu::AcceptsTextInput() const { return IsEnabled(); }
		void ClientMenu::Draw() { Render(); }
	} // namespace client
} // namespace spades
