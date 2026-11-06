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
#include <Client/NetClient.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Label.h>

namespace spades {
	namespace client {
		using gui::PreferenceView;
		using gui::PreferenceViewOptions;
		using gui::ui::Button;
		using gui::ui::Label;
		using gui::ui::UIElement;
		using gui::ui::UIManager;

		namespace {
			// Maps the game mode string announced by the server to a translatable display
			// name. Unknown modes are shown exactly as the server announced them.
			std::string GetGameModeDisplayName(const std::string& raw) {
				std::string trimmed = TrimSpaces(raw);
				if (trimmed.empty())
					return trimmed;

				std::string key = ToLowerCase(trimmed);
				if (key == "ctf")
					return _Tr("Client", "Capture the Flag");
				if (key == "tc")
					return _Tr("Client", "Territory Control");
				if (key == "babel")
					return _Tr("Client", "Tower of Babel");
				if (key == "arena")
					return _Tr("Client", "Arena");
				if (key == "push")
					return _Tr("Client", "Push");
				if (key == "tdm")
					return _Tr("Client", "Team Deathmatch");
				if (key == "ffa")
					return _Tr("Client", "Free for All");

				return trimmed;
			}
		} // namespace

		ClientMenu::ClientMenu(ClientUI* ui)
		    : UIElement(&ui->GetUIManager()), ui(ui), helper(&ui->GetHelper()) {

			UIManager* manager = &GetManager();
			float sw = manager->screenWidth;
			float sh = manager->screenHeight;

			float winW = 180.0F, winH = 32.0F * 4.0F - 2.0F;
			float winX = (sw - winW) * 0.5F;
			float winY = (sh - winH) * 0.5F;

			// draw full background
			{
				Handle<Label> overlay = Handle<Label>::New(manager);
				overlay->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.7F);
				overlay->SetBounds(AABB2(0.0F, 0.0F, sw, sh));
				AddChild(overlay.GetPointerOrNull());
			}

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

			// add resume game hint
			{
				Handle<Label> escHint = Handle<Label>::New(manager);
				escHint->text = _Tr("Client", "[Esc] Back to Game");
				escHint->textColor = MakeVector4(1, 1, 1, 0.7F);
				escHint->SetBounds(AABB2(winX - 8.0F, (winY + winH + 20.0F) - 8.0F, 200.0F, 20.0F));
				AddChild(escHint.GetPointerOrNull());
				this->escHint = escHint.GetPointerOrNull();
			}
		}

		void ClientMenu::OnBackToGame(UIElement&) { ui->SetActiveUI(nullptr); }

		void ClientMenu::OnSetup(UIElement&) { OpenSetup(); }

		void ClientMenu::OpenSetup() {
			PreferenceViewOptions opt;
			opt.gameActive = true;
			opt.persistedState = ui->preferenceState;

			if (escHint)
				escHint->visible = false; // hide while in settings

			Handle<PreferenceView> al =
				Handle<PreferenceView>::New(this, opt, &ui->GetFontManager());
			al->closed = [this](UIElement&) {
				if (escHint)
					escHint->visible = true; // restore on close
			};
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

		void ClientMenu::Render() {
			UIElement::Render();

			IFont& guiFont = ui->GetFontManager().GetGuiFont();
			IFont& headingFont = ui->GetFontManager().GetHeadingFont();

			float x = 8.0F;
			float y = 8.0F;

			Vector4 white = MakeVector4(1, 1, 1, 1);
			Vector4 shadow = MakeVector4(0, 0, 0, 0.5F);

			// draw playing time
			int now = static_cast<int>(helper->GetClientTime());
			int hrs = now / 3600;
			int mins = (now % 3600) / 60;
			int secs = now % 60;

			std::string time;
			if (hrs > 0)
				time = _Tr("Client", "{0}h {1}m {2}s", ToString(hrs), ToString(mins), ToString(secs));
			else if (mins > 0)
				time = _Tr("Client", "{0}m {1}s", ToString(mins), ToString(secs));
			else
				time = _Tr("Client", "{0}s", ToString(secs));

			std::string timeStr = _Tr("Client", "Session time: {0}", time);
			headingFont.DrawShadow(timeStr, MakeVector2(x, y), 1.0F, white, shadow);
			y += headingFont.Measure(timeStr).y + 10.0F;

			// draw server name
			std::string serverName = helper->GetServerName();
			if (!serverName.empty()) {
				std::string serverStr = _Tr("Client", "Connected to: {0}", serverName);
				guiFont.DrawShadow(serverStr, MakeVector2(x, y), 1.0F, white, shadow);
				y += guiFont.Measure(serverStr).y;
			}

			// draw game mode announced by the server list
			std::string gameMode = GetGameModeDisplayName(helper->GetGameMode());
			if (!gameMode.empty()) {
				std::string modeStr = _Tr("Client", "Game mode: {0}", gameMode);
				guiFont.DrawShadow(modeStr, MakeVector2(x, y), 1.0F, white, shadow);
				y += guiFont.Measure(modeStr).y;
			}

			// draw extensions announced by the server
			const auto& extensions = helper->GetServerExtensions();
			if (!extensions.empty()) {
				std::string extStr = _Tr("Client", "Server extensions:");
				guiFont.DrawShadow(extStr, MakeVector2(x, y), 1.0F, white, shadow);
				y += guiFont.Measure(extStr).y;

				for (const auto& ext : extensions) {
					std::string line = GetExtensionDisplayName(ext.id) + " v" + ToString(ext.version);
					// dim unsupported extensions
					Vector4 color = ext.supportedByClient
						? MakeVector4(0.7F, 0.7F, 0.7F, 1.0F)
						: MakeVector4(0.4F, 0.4F, 0.4F, 1.0F);
					guiFont.Draw(line, MakeVector2(x, y), 1.0F, color);
					y += guiFont.Measure(line).y;
				}
			}
		}
	} // namespace client
} // namespace spades
