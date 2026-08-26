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

#include "StartupMainMenu.h"
#include "StartupScreenUI.h"
#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Core/Strings.h>
#include <Gui/StartupScreenHelper.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/TabStrip.h>

namespace spades {
	namespace gui {
		using ui::Button;
		using ui::CheckBox;
		using ui::SimpleButton;
		using ui::SimpleTabStrip;
		using ui::UIElement;
		using ui::UIManager;

		StartupScreenMainMenu::StartupScreenMainMenu(StartupScreenUI* ui)
		    : UIElement(&ui->GetUIManager()),
		      ui(ui),
		      helper(&ui->GetHelper()),
		      cl_showStartupWindow("cl_showStartupWindow", nullptr) {
			UIManager* manager = &GetManager();
			SetFont(&ui->GetFontManager().GetGuiFont());

			float sw = manager->screenWidth;
			float sh = manager->screenHeight;

			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("StartupScreen", "Start");
				button->hotKeyText = _Tr("Client", "[Enter]");
				button->SetBounds(AABB2(sw - 170.0F, 20.0F, 150.0F, 30.0F));
				button->activated = [this](UIElement&) { Start(); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<SimpleButton> button = Handle<SimpleButton>::New(manager);
				button->caption = _Tr("StartupScreen", "GitHub Repository");
				float capSize = GetFont()->Measure(button->caption).x;
				button->SetBounds(
				    AABB2(sw - 170.0F - (capSize + 16.0F) - 10.0F, 20.0F, capSize + 16.0F, 20.0F));
				button->textColor = MakeVector4(0.1F, 0.7F, 1, 1);
				button->activated = [this](UIElement& s) { OnGithubRepositoryPressed(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<SimpleButton> button = Handle<SimpleButton>::New(manager);
				button->caption = _Tr("StartupScreen", "Compatible Mods");
				float capSize = GetFont()->Measure(button->caption).x;
				button->SetBounds(
				    AABB2(sw - 170.0F - (capSize + 16.0F) - 10.0F, 44.0F, capSize + 16.0F, 20.0F));
				button->textColor = MakeVector4(0.1F, 0.7F, 1, 1);
				button->activated = [this](UIElement& s) { OnGithubPaksRepositoryPressed(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<CheckBox> button = Handle<CheckBox>::New(manager);
				button->caption = _Tr("StartupScreen", "Skip this screen next time");
				float capSize = GetFont()->Measure(button->caption).x;
				button->SetBounds(AABB2(8.0F, 45.0F, capSize + 18.0F, 20.0F));
				AddChild(button.GetPointerOrNull());
				bypassStartupWindowCheck = button.GetPointerOrNull();
				button->activated = [this](UIElement& s) {
					OnBypassStartupWindowCheckChanged(s);
				};
			}

			AABB2 clientArea(10.0F, 100.0F, sw - 20.0F, sh - 110.0F);
			Vector2 clientSize = clientArea.max - clientArea.min;

			Handle<StartupScreenGraphicsTab> graphicsTabH =
			    Handle<StartupScreenGraphicsTab>::New(ui, clientSize);
			Handle<StartupScreenAudioTab> audioTabH =
			    Handle<StartupScreenAudioTab>::New(ui, clientSize);
			Handle<StartupScreenGenericTab> genericTabH =
			    Handle<StartupScreenGenericTab>::New(ui, clientSize);
			Handle<StartupScreenSystemInfoTab> profileTabH =
			    Handle<StartupScreenSystemInfoTab>::New(ui, clientSize);
			Handle<StartupScreenAdvancedTab> advancedTabH =
			    Handle<StartupScreenAdvancedTab>::New(ui, clientSize);
			graphicsTabH->SetBounds(clientArea);
			audioTabH->SetBounds(clientArea);
			genericTabH->SetBounds(clientArea);
			profileTabH->SetBounds(clientArea);
			advancedTabH->SetBounds(clientArea);
			AddChild(graphicsTabH.GetPointerOrNull());
			AddChild(audioTabH.GetPointerOrNull());
			AddChild(genericTabH.GetPointerOrNull());
			AddChild(profileTabH.GetPointerOrNull());
			AddChild(advancedTabH.GetPointerOrNull());
			audioTabH->visible = false;
			profileTabH->visible = false;
			genericTabH->visible = false;
			advancedTabH->visible = false;

			graphicsTab = graphicsTabH.GetPointerOrNull();
			audioTab = audioTabH.GetPointerOrNull();
			advancedTab = advancedTabH.GetPointerOrNull();
			systemInfoTab = profileTabH.GetPointerOrNull();
			genericTab = genericTabH.GetPointerOrNull();

			{
				Handle<SimpleTabStrip> tabStrip = Handle<SimpleTabStrip>::New(manager);
				AddChild(tabStrip.GetPointerOrNull());
				tabStrip->SetBounds(AABB2(10.0F, 70.0F, sw - 20.0F, 24.0F));
				tabStrip->AddItem(_Tr("StartupScreen", "Graphics"), graphicsTab);
				tabStrip->AddItem(_Tr("StartupScreen", "Audio"), audioTab);
				tabStrip->AddItem(_Tr("StartupScreen", "Generic"), genericTab);
				tabStrip->AddItem(_Tr("StartupScreen", "System Info"), systemInfoTab);
				tabStrip->AddItem(_Tr("StartupScreen", "Advanced"), advancedTab);
				tabStrip->changed = [this](UIElement& s) { OnTabChanged(s); };
			}

			LoadConfig();
		}

		void StartupScreenMainMenu::OnGithubRepositoryPressed(UIElement&) {
			if (helper->OpenLinkInBrowser("https://github.com/zerospades/zerospades"))
				return;

			std::string msg =
			    _Tr("StartupScreen", "An unknown error has occurred while opening this url.");
			Handle<AlertScreen> al = Handle<AlertScreen>::New(GetParent(), msg, 100.0F);
			al->Run();
		}

		void StartupScreenMainMenu::OnGithubPaksRepositoryPressed(UIElement&) {
			if (helper->OpenLinkInBrowser("https://github.com/zerospades/zerospades-paks"))
				return;

			std::string msg =
			    _Tr("StartupScreen", "An unknown error has occurred while opening this url.");
			Handle<AlertScreen> al = Handle<AlertScreen>::New(GetParent(), msg, 100.0F);
			al->Run();
		}

		void StartupScreenMainMenu::OnTabChanged(UIElement&) {
			bool v = advancedTab->visible;
			if (advancedTabVisible && !v)
				LoadConfig();
			advancedTabVisible = v;
		}

		void StartupScreenMainMenu::LoadConfig() {
			switch (static_cast<int>(cl_showStartupWindow)) {
				case -1: bypassStartupWindowCheck->toggled = false; break;
				case 0: bypassStartupWindowCheck->toggled = true; break;
				default: bypassStartupWindowCheck->toggled = false; break;
			}

			graphicsTab->LoadConfig();
			audioTab->LoadConfig();
			genericTab->LoadConfig();
			advancedTab->LoadConfig();
		}

		StartupScreenMainMenuState StartupScreenMainMenu::GetState() {
			StartupScreenMainMenuState state;
			if (graphicsTab->visible) {
				state.activeTabIndex = 0;
			} else if (audioTab->visible) {
				state.activeTabIndex = 1;
			} else if (genericTab->visible) {
				state.activeTabIndex = 2;
			} else if (systemInfoTab->visible) {
				state.activeTabIndex = 3;
			} else if (advancedTab->visible) {
				state.activeTabIndex = 4;
			}
			return state;
		}

		void StartupScreenMainMenu::SetState(const StartupScreenMainMenuState& state) {
			graphicsTab->visible = state.activeTabIndex == 0;
			audioTab->visible = state.activeTabIndex == 1;
			genericTab->visible = state.activeTabIndex == 2;
			systemInfoTab->visible = state.activeTabIndex == 3;
			advancedTab->visible = state.activeTabIndex == 4;
		}

		void StartupScreenMainMenu::OnBypassStartupWindowCheckChanged(UIElement&) {
			cl_showStartupWindow = (bypassStartupWindowCheck->toggled ? 0 : 1);
		}

		void StartupScreenMainMenu::Start() {
			helper->Start();
			ui->shouldExit = true; // we have to exit from the startup screen to start the game
		}

		void StartupScreenMainMenu::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Enter") {
				Start();
			} else if (IsEnabled() && key == "Escape") {
				ui->shouldExit = true;
			} else {
				UIElement::HotKey(key);
			}
		}
	} // namespace gui
} // namespace spades
