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

#pragma once

#include <Gui/UI/StartupScreen/ConfigViewTabs.h>

namespace spades {
	namespace gui {
		/** State preserved across a startup-screen reload (language change etc.). */
		struct StartupScreenMainMenuState {
			int activeTabIndex = 0;
		};

		/** The startup window: Start button, links, and the settings tabs. */
		class StartupScreenMainMenu : public ui::UIElement {
			StartupScreenUI* ui;         // weak
			StartupScreenHelper* helper; // weak

			ui::CheckBox* bypassStartupWindowCheck;

			StartupScreenGraphicsTab* graphicsTab;
			StartupScreenAudioTab* audioTab;
			StartupScreenGenericTab* genericTab;
			StartupScreenSystemInfoTab* systemInfoTab;
			StartupScreenAdvancedTab* advancedTab;

			Settings::ItemHandle cl_showStartupWindow;
			bool advancedTabVisible = false;

			void OnGithubRepositoryPressed(ui::UIElement&);
			void OnGithubPaksRepositoryPressed(ui::UIElement&);
			void OnTabChanged(ui::UIElement&);
			void OnBypassStartupWindowCheckChanged(ui::UIElement& sender);
			void Start();

		public:
			StartupScreenMainMenu(StartupScreenUI* ui);

			void LoadConfig();

			StartupScreenMainMenuState GetState();
			void SetState(const StartupScreenMainMenuState& state);

			void HotKey(const std::string& key) override;
		};
	} // namespace gui
} // namespace spades
