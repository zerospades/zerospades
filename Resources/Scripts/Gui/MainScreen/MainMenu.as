/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "CreateProfileScreen.as"
#include "ServerList.as"
#include "DemoList.as"

namespace spades {

	class RefreshButton : spades::ui::SimpleButton {
		RefreshButton(spades::ui::UIManager@ manager) { super(manager); }
		void Render() {
			SimpleButton::Render();

			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;
			Image@ img = r.RegisterImage("Gfx/UI/Refresh.png");
			r.DrawImage(img, pos + (size - Vector2(16.0F, 16.0F)) * 0.5F);
		}
	}

	class ProtocolButton : spades::ui::SimpleButton {
		ProtocolButton(spades::ui::UIManager @manager) {
			super(manager);
			Toggle = true;
		}
	}

	uint8 ToLower(uint8 c) {
		if (c >= uint8(0x41) and c <= uint8(0x5a)) {
			return uint8(c - 0x41 + 0x61);
		} else {
			return c;
		}
	}
	bool StringContainsCaseInsensitive(string text, string pattern) {
		for (int i = text.length - 1; i >= 0; i--)
			text[i] = ToLower(text[i]);
		for (int i = pattern.length - 1; i >= 0; i--)
			pattern[i] = ToLower(pattern[i]);
		return text.findFirst(pattern) >= 0;
	}

	// Transparent container panel used for tab switching
	class TabPanel : spades::ui::UIElement {
		TabPanel(spades::ui::UIManager@ manager) { super(manager); }
		void Render() { UIElement::Render(); }
	}

	class MainScreenMainMenu : spades::ui::UIElement {
		MainScreenUI@ ui;
		MainScreenHelper@ helper;
		spades::ui::Field@ addressField;

		spades::ui::Button@ protocol3Button;
		spades::ui::Button@ protocol4Button;

		spades::ui::Button@ filterProtocol3Button;
		spades::ui::Button@ filterProtocol4Button;
		spades::ui::Button@ filterEmptyButton;
		spades::ui::Button@ filterFullButton;
		spades::ui::Field@ filterField;

		spades::ui::ListView@ serverList;
		MainScreenServerListLoadingView@ loadingView;
		MainScreenServerListErrorView@ errorView;
		bool loading = false, loaded = false;

		ServerListModel@ currentServerListModel;
		int serverListUpdateTimer = 5;
		string selectedMapName;

		// Demo tab state
		TabPanel@ demoPanel;
		spades::ui::ListView@ demoList;
		DemoListModel@ currentDemoListModel;
		string selectedDemoPath;
		spades::ui::Field@ demoNameField;
		spades::ui::Button@ demoPlayButton;
		spades::ui::Button@ demoDeleteButton;

		// Demo list column widths (pixels)
		private float demoDateColWidth;
		private float demoModeColWidth;
		private float demoMapColWidth;
		private float demoSizeColWidth;
		private float demoContentsWidth;

		private ConfigItem cg_protocolVersion("cg_protocolVersion", "3");
		private ConfigItem cg_lastQuickConnectHost("cg_lastQuickConnectHost", "127.0.0.1");
		private ConfigItem cg_serverlistSort("cg_serverlistSort", "16385");

		MainScreenMainMenu(MainScreenUI@ ui) {
			super(ui.manager);
			@this.ui = ui;
			@this.helper = ui.helper;
			@this.Font = ui.fontManager.GuiFont;

			demoDateColWidth = 130.0F;
			demoModeColWidth = 85.0F;
			demoMapColWidth  = 185.0F;
			demoSizeColWidth = 70.0F;

			float sw = Manager.ScreenWidth;
			float sh = Manager.ScreenHeight;

			float contentsWidth = sw - 16.0F;
			float maxContentsWidth = 750.0F;
			if (contentsWidth > maxContentsWidth)
				contentsWidth = maxContentsWidth;
			demoContentsWidth = contentsWidth;

			float contentsLeft = (sw - contentsWidth) * 0.5F;
			float footerPos = sh - 50.0F;
			float tabStripPos = 232.0F;
			float headerPos = 260.0F;
			float headerHeight = 24.0F;
			float listPos = headerPos + headerHeight;

			// adjust based on screen width
			float scaleF = Min(sw / maxContentsWidth, 1.0F);
			float slotsOffsetX = 300.0F * scaleF;
			float mapNameOffsetX = 370.0F * scaleF;
			float gameModeOffsetX = 520.0F * scaleF;
			float protocolOffsetX = 640.0F * scaleF;
			float pingOffsetX = 680.0F * scaleF;

			// --- Tab strip (always visible) ---
			{
				TabPanel serverPanel(Manager);
				serverPanel.Bounds = AABB2(0.0F, 0.0F, sw, sh);

				@demoPanel = TabPanel(Manager);
				demoPanel.Bounds = AABB2(0.0F, 0.0F, sw, sh);
				demoPanel.Visible = false;

				// --- Server panel contents ---
				{
					spades::ui::Button button(Manager);
					button.Caption = _Tr("MainScreen", "Connect");
					button.HotKeyText = _Tr("Client", "[Enter]");
					button.Bounds = AABB2(contentsLeft + contentsWidth - 150.0F, 200.0F, 150.0F, 30.0F);
					@button.Activated = spades::ui::EventHandler(this.OnConnectPressed);
					serverPanel.AddChild(button);
				}
				{
					@addressField = spades::ui::Field(Manager);
					addressField.Bounds = AABB2(contentsLeft, 200.0F, contentsWidth - 270.0F, 30.0F);
					addressField.Placeholder = _Tr("MainScreen", "Quick Connect");
					addressField.Text = cg_lastQuickConnectHost.StringValue;
					@addressField.Changed = spades::ui::EventHandler(this.OnAddressChanged);
					serverPanel.AddChild(addressField);
				}
				{
					RefreshButton button(Manager);
					button.Bounds = AABB2(contentsLeft + contentsWidth - 270.0F, 200.0F, 30.0F, 30.0F);
					@button.Activated = spades::ui::EventHandler(this.OnRefreshServerListPressed);
					serverPanel.AddChild(button);
				}
				{
					@protocol3Button = ProtocolButton(Manager);
					protocol3Button.Bounds = AABB2(contentsLeft + contentsWidth - 240.0F + 6.0F, 200.0F, 40.0F, 30.0F);
					protocol3Button.Caption = _Tr("MainScreen", "0.75");
					@protocol3Button.Activated = spades::ui::EventHandler(this.OnProtocol3Pressed);
					protocol3Button.Toggle = true;
					protocol3Button.Toggled = cg_protocolVersion.IntValue == 3;
					serverPanel.AddChild(protocol3Button);
				}
				{
					@protocol4Button = ProtocolButton(Manager);
					protocol4Button.Bounds = AABB2(contentsLeft + contentsWidth - 200.0F + 6.0F, 200.0F, 40.0F, 30.0F);
					protocol4Button.Caption = _Tr("MainScreen", "0.76");
					@protocol4Button.Activated = spades::ui::EventHandler(this.OnProtocol4Pressed);
					protocol4Button.Toggle = true;
					protocol4Button.Toggled = cg_protocolVersion.IntValue == 4;
					serverPanel.AddChild(protocol4Button);
				}
				{
					@serverList = spades::ui::ListView(Manager);
					serverList.Bounds = AABB2(contentsLeft, listPos, contentsWidth, footerPos - listPos - 14.0F);
					serverPanel.AddChild(serverList);
				}
				{
					ServerListHeader header(Manager);
					header.Bounds = AABB2(contentsLeft, headerPos, slotsOffsetX, headerHeight);
					header.Text = _Tr("MainScreen", "Server Name");
					@header.Activated = spades::ui::EventHandler(this.SortServerListByName);
					serverPanel.AddChild(header);
				}
				{
					ServerListHeader header(Manager);
					header.Bounds = AABB2(contentsLeft + slotsOffsetX, headerPos, mapNameOffsetX - slotsOffsetX, headerHeight);
					header.Text = _Tr("MainScreen", "Slots");
					header.Alignment = Vector2(0.5F, 0.5F);
					@header.Activated = spades::ui::EventHandler(this.SortServerListByNumPlayers);
					serverPanel.AddChild(header);
				}
				{
					ServerListHeader header(Manager);
					header.Bounds = AABB2(contentsLeft + mapNameOffsetX, headerPos, gameModeOffsetX - mapNameOffsetX, headerHeight);
					header.Text = _Tr("MainScreen", "Map Name");
					@header.Activated = spades::ui::EventHandler(this.SortServerListByMapName);
					serverPanel.AddChild(header);
				}
				{
					ServerListHeader header(Manager);
					header.Bounds = AABB2(contentsLeft + gameModeOffsetX, headerPos, protocolOffsetX - gameModeOffsetX, headerHeight);
					header.Text = _Tr("MainScreen", "Game Mode");
					header.Alignment = Vector2(0.5F, 0.5F);
					@header.Activated = spades::ui::EventHandler(this.SortServerListByGameMode);
					serverPanel.AddChild(header);
				}
				{
					ServerListHeader header(Manager);
					header.Bounds = AABB2(contentsLeft + protocolOffsetX, headerPos, pingOffsetX - protocolOffsetX, headerHeight);
					header.Text = _Tr("MainScreen", "Ver.");
					header.Alignment = Vector2(0.5F, 0.5F);
					@header.Activated = spades::ui::EventHandler(this.SortServerListByProtocol);
					serverPanel.AddChild(header);
				}
				{
					ServerListHeader header(Manager);
					header.Bounds = AABB2(contentsLeft + pingOffsetX, headerPos, contentsWidth - pingOffsetX, headerHeight);
					header.Text = _Tr("MainScreen", "Ping");
					header.Alignment = Vector2(0.5F, 0.5F);
					@header.Activated = spades::ui::EventHandler(this.SortServerListByPing);
					serverPanel.AddChild(header);
				}
				{
					@loadingView = MainScreenServerListLoadingView(Manager);
					loadingView.Bounds = AABB2(contentsLeft, listPos, contentsWidth, 100.0F);
					loadingView.Visible = false;
					serverPanel.AddChild(loadingView);
				}
				{
					@errorView = MainScreenServerListErrorView(Manager);
					errorView.Bounds = AABB2(contentsLeft, listPos, contentsWidth, 100.0F);
					errorView.Visible = false;
					serverPanel.AddChild(errorView);
				}
				// Server filter footer elements
				{
					spades::ui::Label label(Manager);
					label.Text = _Tr("MainScreen", "Filter");
					label.Bounds = AABB2(contentsLeft, footerPos, 50.0F, 30.0F);
					label.Alignment = Vector2(0.0F, 0.5F);
					serverPanel.AddChild(label);
				}
				{
					@filterProtocol3Button = ProtocolButton(Manager);
					filterProtocol3Button.Bounds = AABB2(contentsLeft + 50.0F, footerPos, 40.0F, 30.0F);
					filterProtocol3Button.Caption = _Tr("MainScreen", "0.75");
					@filterProtocol3Button.Activated = spades::ui::EventHandler(this.OnFilterProtocol3Pressed);
					filterProtocol3Button.Toggle = true;
					serverPanel.AddChild(filterProtocol3Button);
				}
				{
					@filterProtocol4Button = ProtocolButton(Manager);
					filterProtocol4Button.Bounds = AABB2(contentsLeft + 90.0F, footerPos, 40.0F, 30.0F);
					filterProtocol4Button.Caption = _Tr("MainScreen", "0.76");
					@filterProtocol4Button.Activated = spades::ui::EventHandler(this.OnFilterProtocol4Pressed);
					filterProtocol4Button.Toggle = true;
					serverPanel.AddChild(filterProtocol4Button);
				}
				{
					@filterEmptyButton = ProtocolButton(Manager);
					filterEmptyButton.Bounds = AABB2(contentsLeft + 135.0F, footerPos, 50.0F, 30.0F);
					filterEmptyButton.Caption = _Tr("MainScreen", "Empty");
					@filterEmptyButton.Activated = spades::ui::EventHandler(this.OnFilterEmptyPressed);
					filterEmptyButton.Toggle = true;
					serverPanel.AddChild(filterEmptyButton);
				}
				{
					@filterFullButton = ProtocolButton(Manager);
					filterFullButton.Bounds = AABB2(contentsLeft + 185.0F, footerPos, 70.0F, 30.0F);
					filterFullButton.Caption = _Tr("MainScreen", "Not Full");
					@filterFullButton.Activated = spades::ui::EventHandler(this.OnFilterFullPressed);
					filterFullButton.Toggle = true;
					serverPanel.AddChild(filterFullButton);
				}
				{
					@filterField = spades::ui::Field(Manager);
					filterField.Bounds = AABB2(contentsLeft + 260.0F, footerPos, contentsWidth - 567.0F, 30.0F);
					filterField.Placeholder = _Tr("MainScreen", "Filter");
					@filterField.Changed = spades::ui::EventHandler(this.OnFilterTextChanged);
					serverPanel.AddChild(filterField);
				}

				// --- Demo panel contents ---
				{
					// Selected demo name display (mirrors the server address field)
					@demoNameField = spades::ui::Field(Manager);
					demoNameField.Bounds = AABB2(contentsLeft, 200.0F, contentsWidth - 270.0F, 30.0F);
					demoNameField.Placeholder = _Tr("MainScreen", "Select a demo");
					demoNameField.AcceptsFocus = false;
					demoNameField.IsMouseInteractive = false;
					demoPanel.AddChild(demoNameField);
				}
				{
					@demoDeleteButton = spades::ui::Button(Manager);
					demoDeleteButton.Caption = _Tr("MainScreen", "Delete");
					demoDeleteButton.Bounds = AABB2(contentsLeft + contentsWidth - 270.0F, 200.0F, 110.0F, 30.0F);
					@demoDeleteButton.Activated = spades::ui::EventHandler(this.OnDeleteDemoPressed);
					demoPanel.AddChild(demoDeleteButton);
				}
				{
					@demoPlayButton = spades::ui::Button(Manager);
					demoPlayButton.Caption = _Tr("MainScreen", "Play");
					demoPlayButton.HotKeyText = _Tr("Client", "[Enter]");
					demoPlayButton.Bounds = AABB2(contentsLeft + contentsWidth - 150.0F, 200.0F, 150.0F, 30.0F);
					@demoPlayButton.Activated = spades::ui::EventHandler(this.OnPlayDemoPressed);
					demoPanel.AddChild(demoPlayButton);
				}
				{
					// Column order: Server | Map | Mode | Timestamp | Size
					// Server is variable-width; the four rightmost columns are fixed.
					float fixedRight = demoMapColWidth + demoModeColWidth + demoDateColWidth + demoSizeColWidth;
					float serverColWidth = contentsWidth - fixedRight;
					float x = contentsLeft;
					{
						DemoListHeader header(Manager);
						header.Bounds = AABB2(x, headerPos, serverColWidth, headerHeight);
						header.Text = _Tr("MainScreen", "Server");
						demoPanel.AddChild(header);
						x += serverColWidth;
					}
					{
						DemoListHeader header(Manager);
						header.Bounds = AABB2(x, headerPos, demoMapColWidth, headerHeight);
						header.Text = _Tr("MainScreen", "Map");
						demoPanel.AddChild(header);
						x += demoMapColWidth;
					}
					{
						DemoListHeader header(Manager);
						header.Bounds = AABB2(x, headerPos, demoModeColWidth, headerHeight);
						header.Text = _Tr("MainScreen", "Game Mode");
						demoPanel.AddChild(header);
						x += demoModeColWidth;
					}
					{
						DemoListHeader header(Manager);
						header.Bounds = AABB2(x, headerPos, demoDateColWidth, headerHeight);
						header.Text = _Tr("MainScreen", "Timestamp");
						demoPanel.AddChild(header);
						x += demoDateColWidth;
					}
					{
						DemoListHeader header(Manager);
						header.Bounds = AABB2(x, headerPos, demoSizeColWidth, headerHeight);
						header.Text = _Tr("MainScreen", "Size");
						demoPanel.AddChild(header);
					}
				}
				{
					@demoList = spades::ui::ListView(Manager);
					demoList.Bounds = AABB2(contentsLeft, listPos, contentsWidth, footerPos - listPos - 14.0F);
					demoPanel.AddChild(demoList);
				}

				AddChild(serverPanel);
				AddChild(demoPanel);

				// Tab strip
				{
					spades::ui::SimpleTabStrip tabStrip(Manager);
					tabStrip.Bounds = AABB2(contentsLeft, tabStripPos, contentsWidth, 24.0F);
					AddChild(tabStrip);
					tabStrip.AddItem(_Tr("MainScreen", "Servers"), serverPanel);
					tabStrip.AddItem(_Tr("MainScreen", "Demos"), demoPanel);
					@tabStrip.Changed = spades::ui::EventHandler(this.OnTabChanged);
				}
			}

			// Footer buttons (always visible, outside panels)
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("MainScreen", "Quit");
				button.HotKeyText = _Tr("Client", "[Esc]");
				button.Bounds = AABB2(contentsLeft + contentsWidth - 100.0F, footerPos, 100.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnQuitPressed);
				AddChild(button);
			}
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("MainScreen", "Credits");
				button.Bounds = AABB2(contentsLeft + contentsWidth - 202.0F, footerPos, 100.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnCreditsPressed);
				AddChild(button);
			}
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("MainScreen", "Setup");
				button.Bounds = AABB2(contentsLeft + contentsWidth - 304.0F, footerPos, 100.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnSetupPressed);
				AddChild(button);
			}

			LoadServerList();
			LoadDemoList();
		}

		void LoadServerList() {
			if (loading)
				return;
			loaded = false;
			loading = true;
			@serverList.Model = spades::ui::ListViewModel(); // empty
			errorView.Visible = false;
			loadingView.Visible = true;
			helper.StartQuery();
		}

		void LoadDemoList() {
			string[]@ demos = helper.GetDemoList();
			if (demos is null) {
				@demoList.Model = spades::ui::ListViewModel();
				return;
			}
			// Reverse so newest demos appear first
			string[] reversed;
			for (int i = int(demos.length) - 1; i >= 0; i--)
				reversed.insertLast(demos[i]);
			DemoListModel model(Manager, helper, reversed, demoDateColWidth, demoModeColWidth, demoMapColWidth, demoSizeColWidth, demoContentsWidth);
			@demoList.Model = model;
			@model.ItemActivated = DemoListItemEventHandler(this.DemoListItemActivated);
			@model.ItemDoubleClicked = DemoListItemEventHandler(this.DemoListItemDoubleClicked);
			@currentDemoListModel = model;
		}

		private void OnTabChanged(spades::ui::UIElement@ sender) {
			// Refresh demo list when switching to demos tab
			if (demoPanel.Visible)
				LoadDemoList();
		}

		void DemoListItemActivated(DemoListModel@ sender, string filename) {
			selectedDemoPath = filename;
			demoNameField.Text = StripDemoPath(filename);
		}

		void DemoListItemDoubleClicked(DemoListModel@ sender, string filename) {
			selectedDemoPath = filename;
			demoNameField.Text = StripDemoPath(filename);
			PlaySelectedDemo();
		}

		private void PlaySelectedDemo() {
			if (selectedDemoPath.length == 0)
				return;
			string msg = helper.PlayDemo(selectedDemoPath);
			if (msg.length > 0) {
				AlertScreen al(this, msg);
				al.Run();
			}
		}

		private void OnPlayDemoPressed(spades::ui::UIElement@ sender) {
			PlaySelectedDemo();
		}

		private void OnDeleteDemoPressed(spades::ui::UIElement@ sender) {
			if (selectedDemoPath.length == 0)
				return;
			if (!helper.DeleteDemo(selectedDemoPath))
				return;
			selectedDemoPath = "";
			demoNameField.Text = "";
			LoadDemoList();
		}

		void ServerListItemActivated(ServerListModel@ sender, MainScreenServerItem@ item) {
			addressField.Text = item.Address;
			cg_lastQuickConnectHost = addressField.Text;
			selectedMapName = item.MapName;
			if (item.Protocol == "0.75") {
				SetProtocolVersion(3);
			} else if (item.Protocol == "0.76") {
				SetProtocolVersion(4);
			}
			addressField.SelectAll();
		}

		void ServerListItemDoubleClicked(ServerListModel@ sender, MainScreenServerItem@ item) {
			ServerListItemActivated(sender, item);

			// Double-click to connect
			Connect();
		}

		void ServerListItemRightClicked(ServerListModel@ sender, MainScreenServerItem@ item) {
			helper.SetServerFavorite(item.Address, !item.Favorite);
			UpdateServerList();
		}

		private void SortServerListByPing(spades::ui::UIElement@ sender) { SortServerList(0); }
		private void SortServerListByNumPlayers(spades::ui::UIElement@ sender) { SortServerList(1); }
		private void SortServerListByName(spades::ui::UIElement@ sender) { SortServerList(2); }
		private void SortServerListByMapName(spades::ui::UIElement@ sender) { SortServerList(3); }
		private void SortServerListByGameMode(spades::ui::UIElement@ sender) { SortServerList(4); }
		private void SortServerListByProtocol(spades::ui::UIElement@ sender) { SortServerList(5); }
		private void SortServerListByCountry(spades::ui::UIElement@ sender) { SortServerList(6); }

		private void SortServerList(int keyId) {
			int sort = cg_serverlistSort.IntValue;
			if (int(sort & 0xFFF) == keyId) {
				sort ^= int(0x4000);
			} else {
				sort = keyId;
			}
			cg_serverlistSort = sort;
			UpdateServerList();
		}

		private void UpdateServerList(bool refresh = true) {
			string key = "";
			switch (cg_serverlistSort.IntValue & 0xFFF) {
				case 0: key = "Ping"; break;
				case 1: key = "NumPlayers"; break;
				case 2: key = "Name"; break;
				case 3: key = "MapName"; break;
				case 4: key = "GameMode"; break;
				case 5: key = "Protocol"; break;
				case 6: key = "Country"; break;
			}
			MainScreenServerItem @[] @list =
				helper.GetServerList(key, (cg_serverlistSort.IntValue & 0x4000) != 0);
			if ((list is null) or loading) {
				@serverList.Model = spades::ui::ListViewModel(); // empty
				return;
			}

			// filter the server list
			bool filterProtocol3 = filterProtocol3Button.Toggled;
			bool filterProtocol4 = filterProtocol4Button.Toggled;
			bool filterEmpty = filterEmptyButton.Toggled;
			bool filterFull = filterFullButton.Toggled;
			string filterText = filterField.Text;
			MainScreenServerItem @[] @list2 = array<spades::MainScreenServerItem @>();
			for (int i = 0, count = list.length; i < count; i++) {
				MainScreenServerItem@ item = list[i];
				if (filterProtocol3 and (item.Protocol != "0.75"))
					continue;
				if (filterProtocol4 and (item.Protocol != "0.76"))
					continue;
				if (filterEmpty and (item.NumPlayers > 0))
					continue;
				if (filterFull and (item.NumPlayers >= item.MaxPlayers))
					continue;
				if (filterText.length > 0) {
					if (not(StringContainsCaseInsensitive(item.Name, filterText)
						 or StringContainsCaseInsensitive(item.MapName, filterText)
						 or StringContainsCaseInsensitive(item.GameMode, filterText))) {
						continue;
					}
				}
				list2.insertLast(item);
			}

			// If we are updating the list in real time, try not to replace the
			// model
			if (currentServerListModel !is null and !refresh and currentServerListModel.list.length == list2.length) {
				currentServerListModel.ReplaceList(list2);
				return;
			}

			ServerListModel model(Manager, helper, list2);
			@serverList.Model = model;
			@model.ItemActivated = ServerListItemEventHandler(this.ServerListItemActivated);
			@model.ItemDoubleClicked = ServerListItemEventHandler(this.ServerListItemDoubleClicked);
			@model.ItemRightClicked = ServerListItemEventHandler(this.ServerListItemRightClicked);

			@currentServerListModel = model;

			if (refresh)
				serverList.ScrollToTop();
		}

		private void CheckServerList() {
			if (helper.PollServerListState()) {
				MainScreenServerItem @[] @list = helper.GetServerList("", false);
				if (list is null or list.length == 0) {
					// failed.
					// FIXME: show error message?
					loaded = false;
					loading = false;
					errorView.Visible = true;
					loadingView.Visible = false;
					@serverList.Model = spades::ui::ListViewModel(); // empty
					return;
				}
				loading = false;
				loaded = true;
				errorView.Visible = false;
				loadingView.Visible = false;
				UpdateServerList();
			}

			if ((cg_serverlistSort.IntValue & 0xfff) == 0 and loaded) {
				// Ping (RTT) is updated in real-time
				if (serverListUpdateTimer == 0) {
					UpdateServerList(false);
					serverListUpdateTimer = 5;
				}
				--serverListUpdateTimer;
			}
		}

		private void OnAddressChanged(spades::ui::UIElement@ sender) {
			cg_lastQuickConnectHost = addressField.Text;
			selectedMapName = "";
		}

		private void SetProtocolVersion(int ver) {
			protocol3Button.Toggled = (ver == 3);
			protocol4Button.Toggled = (ver == 4);
			cg_protocolVersion = ver;
		}

		private void OnProtocol3Pressed(spades::ui::UIElement@ sender) { SetProtocolVersion(3); }
		private void OnProtocol4Pressed(spades::ui::UIElement@ sender) { SetProtocolVersion(4); }

		private void OnFilterProtocol3Pressed(spades::ui::UIElement@ sender) {
			filterProtocol4Button.Toggled = false;
			UpdateServerList();
		}
		private void OnFilterProtocol4Pressed(spades::ui::UIElement@ sender) {
			filterProtocol3Button.Toggled = false;
			UpdateServerList();
		}
		private void OnFilterFullPressed(spades::ui::UIElement@ sender) {
			filterEmptyButton.Toggled = false;
			UpdateServerList();
		}
		private void OnFilterEmptyPressed(spades::ui::UIElement@ sender) {
			filterFullButton.Toggled = false;
			UpdateServerList();
		}
		private void OnFilterTextChanged(spades::ui::UIElement@ sender) {
			UpdateServerList();
		}

		private void OnRefreshServerListPressed(spades::ui::UIElement@ sender) { LoadServerList(); }
		private void OnQuitPressed(spades::ui::UIElement@ sender) { ui.shouldExit = true; }
		private void OnCreditsPressed(spades::ui::UIElement@ sender) {
			AlertScreen al(this, ui.helper.Credits, Min(500.0F, Manager.ScreenHeight - 100.0F));
			al.Run();
		}

		private void OnSetupPressed(spades::ui::UIElement@ sender) {
			PreferenceView al(this, PreferenceViewOptions(), ui.fontManager);
			al.Run();
		}

		private void Connect() {
			// Pass selectedMapName if known (server clicked in list); otherwise pass empty
			// and let ConnectServer resolve it from the cached server list on the C++ side.
			string msg = helper.ConnectServer(addressField.Text, cg_protocolVersion.IntValue, selectedMapName);
			if (msg.length > 0) {
				AlertScreen al(this, msg);
				al.Run();
			}
		}

		private void OnConnectPressed(spades::ui::UIElement@ sender) { Connect(); }

		void HotKey(string key) {
			if (IsEnabled and key == "Enter") {
				if (demoPanel.Visible) {
					PlaySelectedDemo();
				} else {
					Connect();
				}
			} else if (IsEnabled and key == "Escape") {
				ui.shouldExit = true;
			} else {
				UIElement::HotKey(key);
			}
		}

		void Render() {
			CheckServerList();
			UIElement::Render();

			// check for client error message.
			if (IsEnabled) {
				string msg = helper.GetPendingErrorMessage();
				if (msg.length > 0) {
					// try to make the "disconnected" message more friendly.
					string findStr = "Disconnected:";
					if (msg.findFirst(findStr) >= 0) {
						int ind1 = msg.findFirst(findStr);
						int ind2 = msg.findFirst("\n", ind1);
						if (ind2 < 0)
							ind2 = msg.length;
						ind1 += findStr.length;
						msg = msg.substr(ind1, ind2 - ind1);
						msg = _Tr(
							"MainScreen",
							"You were disconnected from the server because of the following reason:\n\n{0}",
							msg);
					}

					// failed to connect.
					AlertScreen al(this, msg);
					al.Run();
				}
			}
		}
	}
}
