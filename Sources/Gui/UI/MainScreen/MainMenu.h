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

#include <string>
#include <vector>

#include <Gui/UI/MainScreen/DemoListView.h>
#include <Gui/UI/MainScreen/KV6BrowserPanel.h>
#include <Gui/UI/MainScreen/ModListView.h>
#include <Gui/UI/MainScreen/ServerListView.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Field.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/TabStrip.h>

namespace spades {
	namespace gui {
		class MainScreenUI;
		class MainScreenHelper;
		class ModsScreenHelper;

		/** A `SimpleButton` decorated with the refresh arrow icon. */
		class RefreshButton : public ui::SimpleButton {
		public:
			RefreshButton(ui::UIManager* manager) : ui::SimpleButton(manager) {}
			void Render() override;
		};

		/** A toggle `SimpleButton` used for protocol selection/filtering. */
		class ProtocolButton : public ui::SimpleButton {
		public:
			ProtocolButton(ui::UIManager* manager) : ui::SimpleButton(manager) { toggle = true; }
		};

		/** Transparent container panel used for tab switching. */
		class TabPanel : public ui::UIElement {
		public:
			TabPanel(ui::UIManager* manager) : ui::UIElement(manager) {}
		};

		/** Modal dialog for renaming a demo file. */
		class RenameScreen : public ui::UIElement {
			ui::UIElement* owner; // weak
			ui::Field* nameField; // weak; owned as a child

			void OnConfirm(ui::UIElement& sender);
			void OnCancel(ui::UIElement& sender);

		public:
			ui::EventHandler closed;
			bool result = false;
			std::string newName;

			RenameScreen(ui::UIElement* owner, const std::string& currentName);

			void Close();
			void Run();
			void HotKey(const std::string& key) override;
		};

		/**
		 * The part of the main menu that survives a rebuild. Everything else either
		 * lives in config (the quick-connect address, the protocol choice) or in the
		 * helper (the server list), and comes back on its own.
		 */
		struct MainScreenMainMenuState {
			int activeTabIndex = 0;
		};

		/** The main menu: server browser, demo browser, mod manager, footer. */
		class MainScreenMainMenu : public ui::UIElement {
			MainScreenUI* ui;         // weak
			MainScreenHelper* helper; // weak

			ui::Field* addressField;

			ui::Button* protocol3Button;
			ui::Button* protocol4Button;

			ui::Button* filterProtocol3Button;
			ui::Button* filterProtocol4Button;
			ui::Button* filterFullButton;
			ui::Field* filterField;

			ui::ListView* serverList;
			MainScreenServerListLoadingView* loadingView;
			MainScreenServerListErrorView* errorView;
			bool loading = false, loaded = false;

			Handle<ServerListModel> currentServerListModel;
			int serverListUpdateTimer = 5;
			std::string selectedMapName;

			TabPanel* serverPanel;

			// Demo tab state
			TabPanel* demoPanel;
			ui::ListView* demoList;
			Handle<DemoListModel> currentDemoListModel;
			std::string selectedDemoPath;
			ui::Field* demoNameField;
			ui::Button* demoPlayButton;
			ui::Button* demoDeleteButton;
			ui::Button* demoRenameButton;

			// Demo list column widths (pixels)
			float demoDateColWidth;
			float demoModeColWidth;
			float demoMapColWidth;
			float demoSizeColWidth;
			float demoContentsWidth;

			// Editor tab state (self-contained file-browser panel)
			KV6BrowserPanel* editorPanel;

			// Mods tab state
			Handle<ModsScreenHelper> modsHelper;
			TabPanel* modsPanel;
			ui::ListView* modsList;
			ui::Button* modsDownloadButton;
			ui::Button* modsApplyButton;
			ui::Button* modsResetButton;
			ui::Label* modsStatusLabel;
			ModsProgressBar* modsProgressBar;
			bool modsDownloading = false;
			bool modsDirty = false; // enabled set changed this session; restart to apply
			float modsCheckColWidth;
			float modsOrderColWidth;
			float modsTagColWidth;
			float modsNameColWidth;
			float modsAuthorColWidth;
			float modsSizeColWidth;

			void OnTabChanged(ui::UIElement& sender);
			int EnabledIndex(const std::vector<std::string>& enabled, const std::string& name);
			void UpdateModsStatus();
			void OnDownloadModsPressed(ui::UIElement& sender);
			void OnDownloadConfirmed(ui::UIElement& sender);
			void OnApplyModsPressed(ui::UIElement& sender);
			void OnResetModsPressed(ui::UIElement& sender);
			void OnResetConfirmed(ui::UIElement& sender);
			void OnModToggle(const std::string& modName);
			void CheckModsRefresh();

			void DemoListItemActivated(const std::string& filename);
			void DemoListItemDoubleClicked(const std::string& filename);
			void PlaySelectedDemo();
			void OnPlayDemoPressed(ui::UIElement& sender);
			void OnDeleteDemoPressed(ui::UIElement& sender);
			void OnDeleteConfirmClosed(ui::UIElement& sender);
			void OnRenameDemoPressed(ui::UIElement& sender);
			void OnRenameScreenClosed(ui::UIElement& sender);

			void ServerListItemActivated(MainScreenServerItem& item);
			void ServerListItemDoubleClicked(MainScreenServerItem& item);
			void ServerListItemRightClicked(MainScreenServerItem& item);

			void SortServerList(int keyId);
			void UpdateServerList(bool refresh = true);
			void CheckServerList();

			void OnAddressChanged(ui::UIElement& sender);
			void SetProtocolVersion(int ver);

			void OnQuitPressed(ui::UIElement& sender);
			void OnCreditsPressed(ui::UIElement& sender);
			void OnSetupPressed(ui::UIElement& sender);
			void Connect();

		public:
			MainScreenMainMenu(MainScreenUI* ui);

			void LoadServerList();
			void LoadDemoList();
			void LoadModList();

			MainScreenMainMenuState GetState();
			void SetState(const MainScreenMainMenuState& state);

			void HotKey(const std::string& key) override;
			void Render() override;
		};
	} // namespace gui
} // namespace spades
