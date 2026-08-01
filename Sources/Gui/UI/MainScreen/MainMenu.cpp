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

#include <algorithm>
#include <cctype>

#include "MainMenu.h"
#include "MainScreenUI.h"
#include <Client/Fonts.h>
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Gui/MainScreenHelper.h>
#include <Gui/ModsScreenHelper.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Settings/PreferenceView.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/MessageBox.h>

DEFINE_SPADES_SETTING(cg_protocolVersion, "3");
DEFINE_SPADES_SETTING(cg_lastQuickConnectHost, "127.0.0.1");
DEFINE_SPADES_SETTING(cg_serverlistSort, "16385");

namespace spades {
	namespace gui {
		using ui::Button;
		using ui::Field;
		using ui::Label;
		using ui::ListView;
		using ui::ListViewModel;
		using ui::SetColorNP;
		using ui::SimpleTabStrip;
		using ui::UIElement;
		using ui::UIManager;

		namespace {
			bool StringContainsCaseInsensitive(std::string text, std::string pattern) {
				for (char& c : text)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				for (char& c : pattern)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				return text.find(pattern) != std::string::npos;
			}
		} // namespace

		// -- RefreshButton --

		void RefreshButton::Render() {
			SimpleButton::Render();

			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;
			Handle<client::IImage> img = r.RegisterImage("Gfx/UI/Refresh.png");
			r.DrawImage(img, pos + (sz - MakeVector2(16.0F, 16.0F)) * 0.5F);
		}

		// -- RenameScreen --

		RenameScreen::RenameScreen(UIElement* owner, const std::string& currentName)
		    : UIElement(&owner->GetManager()), owner(owner) {
			SetFont(GetManager().GetRootElement().GetFont());
			SetBounds(owner->GetBounds());

			UIManager* manager = &GetManager();
			float sw = manager->screenWidth;
			float sh = manager->screenHeight;
			float w = std::min(sw - 16.0F, 500.0F);
			float h = 160.0F;
			float x = (sw - w) * 0.5F;
			float y = (sh - h) * 0.5F;

			// Background
			{
				Handle<Label> bg = Handle<Label>::New(manager);
				bg->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.9F);
				bg->SetBounds(AABB2(0.0F, y - 13.0F, size.x, h + 27.0F));
				AddChild(bg.GetPointerOrNull());
			}
			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->text = _Tr("MainScreen", "Rename Demo");
				label->SetBounds(AABB2(x, y, w, 30.0F));
				label->alignment = MakeVector2(0.0F, 0.5F);
				AddChild(label.GetPointerOrNull());
			}
			{
				Handle<Field> field = Handle<Field>::New(manager);
				nameField = field.GetPointerOrNull();
				nameField->SetBounds(AABB2(x, y + 40.0F, w, 30.0F));
				nameField->SetText(currentName);
				nameField->SelectAll();
				AddChild(nameField);
			}
			{
				Handle<Button> btn = Handle<Button>::New(manager);
				btn->caption = _Tr("MainScreen", "Rename");
				btn->SetBounds(AABB2(x + w - 320.0F, y + 90.0F, 150.0F, 30.0F));
				btn->activated = [this](UIElement& s) { OnConfirm(s); };
				AddChild(btn.GetPointerOrNull());
			}
			{
				Handle<Button> btn = Handle<Button>::New(manager);
				btn->caption = _Tr("MainScreen", "Cancel");
				btn->SetBounds(AABB2(x + w - 160.0F, y + 90.0F, 150.0F, 30.0F));
				btn->activated = [this](UIElement& s) { OnCancel(s); };
				AddChild(btn.GetPointerOrNull());
			}
		}

		void RenameScreen::OnConfirm(UIElement&) {
			newName = nameField->GetText();
			result = true;
			Close();
		}

		void RenameScreen::OnCancel(UIElement&) {
			result = false;
			Close();
		}

		void RenameScreen::Close() {
			// keep ourselves alive while handlers observing the close run
			Handle<RenameScreen> keepAlive(this);
			owner->enable = true;
			GetParent()->RemoveChild(this);
			if (closed)
				closed(*this);
		}

		void RenameScreen::Run() {
			owner->enable = false;
			owner->GetParent()->AddChild(this);
			GetManager().SetActiveElement(nameField);
		}

		void RenameScreen::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Enter") {
				OnConfirm(*this);
			} else if (IsEnabled() && key == "Escape") {
				OnCancel(*this);
			} else {
				UIElement::HotKey(key);
			}
		}

		// -- MainScreenMainMenu --

		MainScreenMainMenu::MainScreenMainMenu(MainScreenUI* ui)
		    : UIElement(&ui->GetUIManager()), ui(ui), helper(&ui->GetHelper()) {
			SetFont(&ui->GetFontManager().GetGuiFont());

			UIManager* manager = &GetManager();

			demoDateColWidth = 130.0F;
			demoModeColWidth = 125.0F;
			demoMapColWidth = 185.0F;
			demoSizeColWidth = 70.0F;

			modsCheckColWidth = 60.0F;
			modsOrderColWidth = 90.0F;
			modsTagColWidth = 56.0F;
			modsNameColWidth = 200.0F;
			modsAuthorColWidth = 150.0F;
			modsSizeColWidth = 100.0F;
			modsHelper = Handle<ModsScreenHelper>::New();

			float sw = manager->screenWidth;
			float sh = manager->screenHeight;

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
			float scaleF = std::min(sw / maxContentsWidth, 1.0F);
			float slotsOffsetX = 300.0F * scaleF;
			float mapNameOffsetX = 370.0F * scaleF;
			float gameModeOffsetX = 520.0F * scaleF;
			float protocolOffsetX = 640.0F * scaleF;
			float pingOffsetX = 680.0F * scaleF;

			// --- Tab strip (always visible) ---
			{
				Handle<TabPanel> serverPanelH = Handle<TabPanel>::New(manager);
				TabPanel* serverPanel = serverPanelH.GetPointerOrNull();
				serverPanel->SetBounds(AABB2(0.0F, 0.0F, sw, sh));

				Handle<TabPanel> demoPanelH = Handle<TabPanel>::New(manager);
				demoPanel = demoPanelH.GetPointerOrNull();
				demoPanel->SetBounds(AABB2(0.0F, 0.0F, sw, sh));
				demoPanel->visible = false;

				Handle<TabPanel> modsPanelH = Handle<TabPanel>::New(manager);
				modsPanel = modsPanelH.GetPointerOrNull();
				modsPanel->SetBounds(AABB2(0.0F, 0.0F, sw, sh));
				modsPanel->visible = false;

				// --- Server panel contents ---
				{
					Handle<Button> button = Handle<Button>::New(manager);
					button->caption = _Tr("MainScreen", "Connect");
					button->hotKeyText = _Tr("Client", "[Enter]");
					button->SetBounds(
					    AABB2(contentsLeft + contentsWidth - 160.0F, 200.0F, 160.0F, 30.0F));
					button->activated = [this](UIElement&) { Connect(); };
					serverPanel->AddChild(button.GetPointerOrNull());
				}
				{
					Handle<Field> field = Handle<Field>::New(manager);
					addressField = field.GetPointerOrNull();
					addressField->SetBounds(
					    AABB2(contentsLeft, 200.0F, contentsWidth - 280.0F, 30.0F));
					addressField->placeholder = _Tr("MainScreen", "Quick Connect");
					addressField->SetText(cg_lastQuickConnectHost);
					addressField->changed = [this](UIElement& s) { OnAddressChanged(s); };
					serverPanel->AddChild(addressField);
				}
				{
					Handle<RefreshButton> button = Handle<RefreshButton>::New(manager);
					button->SetBounds(
					    AABB2(contentsLeft + contentsWidth - 278.0F, 200.0F, 30.0F, 30.0F));
					button->activated = [this](UIElement&) { LoadServerList(); };
					serverPanel->AddChild(button.GetPointerOrNull());
				}
				{
					Handle<ProtocolButton> button = Handle<ProtocolButton>::New(manager);
					protocol3Button = button.GetPointerOrNull();
					protocol3Button->SetBounds(
					    AABB2(contentsLeft + contentsWidth - 245.0F, 200.0F, 40.0F, 30.0F));
					protocol3Button->caption = _Tr("MainScreen", "0.75");
					protocol3Button->activated = [this](UIElement&) { SetProtocolVersion(3); };
					protocol3Button->toggle = true;
					protocol3Button->toggled = static_cast<int>(cg_protocolVersion) == 3;
					serverPanel->AddChild(protocol3Button);
				}
				{
					Handle<ProtocolButton> button = Handle<ProtocolButton>::New(manager);
					protocol4Button = button.GetPointerOrNull();
					protocol4Button->SetBounds(
					    AABB2(contentsLeft + contentsWidth - 205.0F, 200.0F, 40.0F, 30.0F));
					protocol4Button->caption = _Tr("MainScreen", "0.76");
					protocol4Button->activated = [this](UIElement&) { SetProtocolVersion(4); };
					protocol4Button->toggle = true;
					protocol4Button->toggled = static_cast<int>(cg_protocolVersion) == 4;
					serverPanel->AddChild(protocol4Button);
				}
				{
					Handle<ListView> list = Handle<ListView>::New(manager);
					serverList = list.GetPointerOrNull();
					serverList->SetBounds(
					    AABB2(contentsLeft, listPos, contentsWidth, footerPos - listPos - 14.0F));
					serverPanel->AddChild(serverList);
				}
				{
					Handle<ServerListHeader> header = Handle<ServerListHeader>::New(manager);
					header->SetBounds(AABB2(contentsLeft, headerPos, slotsOffsetX, headerHeight));
					header->text = _Tr("MainScreen", "Server Name");
					header->activated = [this](UIElement&) { SortServerList(2); };
					serverPanel->AddChild(header.GetPointerOrNull());
				}
				{
					Handle<ServerListHeader> header = Handle<ServerListHeader>::New(manager);
					header->SetBounds(AABB2(contentsLeft + slotsOffsetX, headerPos,
					                        mapNameOffsetX - slotsOffsetX, headerHeight));
					header->text = _Tr("MainScreen", "Slots");
					header->alignment = MakeVector2(0.5F, 0.5F);
					header->activated = [this](UIElement&) { SortServerList(1); };
					serverPanel->AddChild(header.GetPointerOrNull());
				}
				{
					Handle<ServerListHeader> header = Handle<ServerListHeader>::New(manager);
					header->SetBounds(AABB2(contentsLeft + mapNameOffsetX, headerPos,
					                        gameModeOffsetX - mapNameOffsetX, headerHeight));
					header->text = _Tr("MainScreen", "Map Name");
					header->activated = [this](UIElement&) { SortServerList(3); };
					serverPanel->AddChild(header.GetPointerOrNull());
				}
				{
					Handle<ServerListHeader> header = Handle<ServerListHeader>::New(manager);
					header->SetBounds(AABB2(contentsLeft + gameModeOffsetX, headerPos,
					                        protocolOffsetX - gameModeOffsetX, headerHeight));
					header->text = _Tr("MainScreen", "Game Mode");
					header->alignment = MakeVector2(0.5F, 0.5F);
					header->activated = [this](UIElement&) { SortServerList(4); };
					serverPanel->AddChild(header.GetPointerOrNull());
				}
				{
					Handle<ServerListHeader> header = Handle<ServerListHeader>::New(manager);
					header->SetBounds(AABB2(contentsLeft + protocolOffsetX, headerPos,
					                        pingOffsetX - protocolOffsetX, headerHeight));
					header->text = _Tr("MainScreen", "Ver.");
					header->alignment = MakeVector2(0.5F, 0.5F);
					header->activated = [this](UIElement&) { SortServerList(5); };
					serverPanel->AddChild(header.GetPointerOrNull());
				}
				{
					Handle<ServerListHeader> header = Handle<ServerListHeader>::New(manager);
					header->SetBounds(AABB2(contentsLeft + pingOffsetX, headerPos,
					                        contentsWidth - pingOffsetX, headerHeight));
					header->text = _Tr("MainScreen", "Ping");
					header->alignment = MakeVector2(0.5F, 0.5F);
					header->activated = [this](UIElement&) { SortServerList(0); };
					serverPanel->AddChild(header.GetPointerOrNull());
				}
				{
					Handle<MainScreenServerListLoadingView> view =
					    Handle<MainScreenServerListLoadingView>::New(manager);
					loadingView = view.GetPointerOrNull();
					loadingView->SetBounds(AABB2(contentsLeft, listPos, contentsWidth, 100.0F));
					loadingView->visible = false;
					serverPanel->AddChild(loadingView);
				}
				{
					Handle<MainScreenServerListErrorView> view =
					    Handle<MainScreenServerListErrorView>::New(manager);
					errorView = view.GetPointerOrNull();
					errorView->SetBounds(AABB2(contentsLeft, listPos, contentsWidth, 100.0F));
					errorView->visible = false;
					serverPanel->AddChild(errorView);
				}

				// Server filter footer elements
				{
					Handle<Label> label = Handle<Label>::New(manager);
					label->text = _Tr("MainScreen", "Filter");
					label->SetBounds(AABB2(contentsLeft, footerPos, 50.0F, 30.0F));
					label->alignment = MakeVector2(0.0F, 0.5F);
					serverPanel->AddChild(label.GetPointerOrNull());
				}
				{
					Handle<ProtocolButton> button = Handle<ProtocolButton>::New(manager);
					filterProtocol3Button = button.GetPointerOrNull();
					filterProtocol3Button->SetBounds(
					    AABB2(contentsLeft + 50.0F, footerPos, 40.0F, 30.0F));
					filterProtocol3Button->caption = _Tr("MainScreen", "0.75");
					filterProtocol3Button->activated = [this](UIElement&) {
						filterProtocol4Button->toggled = false;
						UpdateServerList();
					};
					filterProtocol3Button->toggle = true;
					serverPanel->AddChild(filterProtocol3Button);
				}
				{
					Handle<ProtocolButton> button = Handle<ProtocolButton>::New(manager);
					filterProtocol4Button = button.GetPointerOrNull();
					filterProtocol4Button->SetBounds(
					    AABB2(contentsLeft + 90.0F, footerPos, 40.0F, 30.0F));
					filterProtocol4Button->caption = _Tr("MainScreen", "0.76");
					filterProtocol4Button->activated = [this](UIElement&) {
						filterProtocol3Button->toggled = false;
						UpdateServerList();
					};
					filterProtocol4Button->toggle = true;
					serverPanel->AddChild(filterProtocol4Button);
				}

				{
					Handle<ProtocolButton> button = Handle<ProtocolButton>::New(manager);
					filterFullButton = button.GetPointerOrNull();
					filterFullButton->SetBounds(
					    AABB2(contentsLeft + 135.0F, footerPos, 70.0F, 30.0F));
					filterFullButton->caption = _Tr("MainScreen", "Not Full");
					filterFullButton->activated = [this](UIElement&) { UpdateServerList(); };
					filterFullButton->toggle = true;
					serverPanel->AddChild(filterFullButton);
				}

				{
					Handle<Field> field = Handle<Field>::New(manager);
					filterField = field.GetPointerOrNull();
					filterField->SetBounds(AABB2(contentsLeft + 210.0F, footerPos, 100.0F, 30.0F));
					filterField->placeholder = _Tr("MainScreen", "Filter");
					filterField->changed = [this](UIElement&) { UpdateServerList(); };
					serverPanel->AddChild(filterField);
				}

				// --- Demo panel contents ---
				{
					// Selected demo name display (mirrors the server address field)
					Handle<Field> field = Handle<Field>::New(manager);
					demoNameField = field.GetPointerOrNull();
					demoNameField->SetBounds(
					    AABB2(contentsLeft, 200.0F, contentsWidth - 320.0F, 30.0F));
					demoNameField->placeholder = _Tr("MainScreen", "Select a demo");
					demoNameField->acceptsFocus = false;
					demoNameField->isMouseInteractive = false;
					demoPanel->AddChild(demoNameField);
				}
				{
					Handle<Button> button = Handle<Button>::New(manager);
					demoDeleteButton = button.GetPointerOrNull();
					demoDeleteButton->caption = _Tr("MainScreen", "Delete");
					demoDeleteButton->SetBounds(
					    AABB2(contentsLeft + contentsWidth - 320.0F, 200.0F, 80.0F, 30.0F));
					demoDeleteButton->activated = [this](UIElement& s) { OnDeleteDemoPressed(s); };
					demoPanel->AddChild(demoDeleteButton);
				}
				{
					Handle<Button> button = Handle<Button>::New(manager);
					demoRenameButton = button.GetPointerOrNull();
					demoRenameButton->caption = _Tr("MainScreen", "Rename");
					demoRenameButton->SetBounds(
					    AABB2(contentsLeft + contentsWidth - 240.0F, 200.0F, 90.0F, 30.0F));
					demoRenameButton->activated = [this](UIElement& s) { OnRenameDemoPressed(s); };
					demoPanel->AddChild(demoRenameButton);
				}
				{
					Handle<Button> button = Handle<Button>::New(manager);
					demoPlayButton = button.GetPointerOrNull();
					demoPlayButton->caption = _Tr("MainScreen", "Play");
					demoPlayButton->hotKeyText = _Tr("Client", "[Enter]");
					demoPlayButton->SetBounds(
					    AABB2(contentsLeft + contentsWidth - 150.0F, 200.0F, 150.0F, 30.0F));
					demoPlayButton->activated = [this](UIElement& s) { OnPlayDemoPressed(s); };
					demoPanel->AddChild(demoPlayButton);
				}
				{
					// Column order: Server | Map | Mode | Timestamp | Size
					// Server is variable-width; the four rightmost columns are fixed.
					float fixedRight =
					    demoMapColWidth + demoModeColWidth + demoDateColWidth + demoSizeColWidth;
					float serverColWidth = contentsWidth - fixedRight;
					float x = contentsLeft;
					{
						Handle<DemoListHeader> header = Handle<DemoListHeader>::New(manager);
						header->SetBounds(AABB2(x, headerPos, serverColWidth, headerHeight));
						header->text = _Tr("MainScreen", "Server");
						demoPanel->AddChild(header.GetPointerOrNull());
						x += serverColWidth;
					}
					{
						Handle<DemoListHeader> header = Handle<DemoListHeader>::New(manager);
						header->SetBounds(AABB2(x, headerPos, demoMapColWidth, headerHeight));
						header->text = _Tr("MainScreen", "Map");
						demoPanel->AddChild(header.GetPointerOrNull());
						x += demoMapColWidth;
					}
					{
						Handle<DemoListHeader> header = Handle<DemoListHeader>::New(manager);
						header->SetBounds(AABB2(x, headerPos, demoModeColWidth, headerHeight));
						header->text = _Tr("MainScreen", "Game Mode");
						demoPanel->AddChild(header.GetPointerOrNull());
						x += demoModeColWidth;
					}
					{
						Handle<DemoListHeader> header = Handle<DemoListHeader>::New(manager);
						header->SetBounds(AABB2(x, headerPos, demoDateColWidth, headerHeight));
						header->text = _Tr("MainScreen", "Timestamp");
						demoPanel->AddChild(header.GetPointerOrNull());
						x += demoDateColWidth;
					}
					{
						Handle<DemoListHeader> header = Handle<DemoListHeader>::New(manager);
						header->SetBounds(AABB2(x, headerPos, demoSizeColWidth, headerHeight));
						header->text = _Tr("MainScreen", "Size");
						demoPanel->AddChild(header.GetPointerOrNull());
					}
				}
				{
					Handle<ListView> list = Handle<ListView>::New(manager);
					demoList = list.GetPointerOrNull();
					demoList->SetBounds(
					    AABB2(contentsLeft, listPos, contentsWidth, footerPos - listPos - 14.0F));
					demoPanel->AddChild(demoList);
				}

				// --- Mods panel contents ---
				{
					Handle<Button> button = Handle<Button>::New(manager);
					modsDownloadButton = button.GetPointerOrNull();
					modsDownloadButton->caption = _Tr("MainScreen", "Download official mods...");
					modsDownloadButton->SetBounds(AABB2(contentsLeft, 200.0F, 240.0F, 30.0F));
					modsDownloadButton->activated = [this](UIElement& s) {
						OnDownloadModsPressed(s);
					};
					modsPanel->AddChild(modsDownloadButton);
				}
				{
					Handle<Button> button = Handle<Button>::New(manager);
					modsResetButton = button.GetPointerOrNull();
					modsResetButton->caption = _Tr("MainScreen", "Disable all");
					modsResetButton->SetBounds(
					    AABB2(contentsLeft + contentsWidth - 270.0F, 200.0F, 130.0F, 30.0F));
					modsResetButton->activated = [this](UIElement& s) { OnResetModsPressed(s); };
					modsPanel->AddChild(modsResetButton);
				}
				{
					Handle<Button> button = Handle<Button>::New(manager);
					modsApplyButton = button.GetPointerOrNull();
					modsApplyButton->caption = _Tr("MainScreen", "Apply changes");
					modsApplyButton->SetBounds(
					    AABB2(contentsLeft + contentsWidth - 130.0F, 200.0F, 130.0F, 30.0F));
					modsApplyButton->activated = [this](UIElement& s) { OnApplyModsPressed(s); };
					modsPanel->AddChild(modsApplyButton);
				}
				{
					Handle<Label> label = Handle<Label>::New(manager);
					modsStatusLabel = label.GetPointerOrNull();
					modsStatusLabel->SetBounds(
					    AABB2(contentsLeft, footerPos - 40.0F, contentsWidth, 24.0F));
					modsStatusLabel->text = "";
					modsPanel->AddChild(modsStatusLabel);
				}
				{
					Handle<ModsProgressBar> bar = Handle<ModsProgressBar>::New(manager);
					modsProgressBar = bar.GetPointerOrNull();
					modsProgressBar->SetBounds(
					    AABB2(contentsLeft, footerPos - 12.0F, contentsWidth, 6.0F));
					modsProgressBar->visible = false;
					modsPanel->AddChild(modsProgressBar);
				}
				{
					float x = contentsLeft;
					{
						Handle<ModListHeader> header = Handle<ModListHeader>::New(manager);
						header->SetBounds(AABB2(x, headerPos, modsCheckColWidth, headerHeight));
						header->text = _Tr("MainScreen", "Status");
						modsPanel->AddChild(header.GetPointerOrNull());
						x += modsCheckColWidth;
					}
					{
						Handle<ModListHeader> header = Handle<ModListHeader>::New(manager);
						header->SetBounds(AABB2(x, headerPos, modsOrderColWidth, headerHeight));
						header->text = _Tr("MainScreen", "Apply Order");
						modsPanel->AddChild(header.GetPointerOrNull());
						x += modsOrderColWidth;
					}
					{
						Handle<ModListHeader> header = Handle<ModListHeader>::New(manager);
						header->SetBounds(AABB2(x, headerPos, modsTagColWidth, headerHeight));
						header->text = _Tr("MainScreen", "Type");
						modsPanel->AddChild(header.GetPointerOrNull());
						x += modsTagColWidth;
					}
					{
						Handle<ModListHeader> header = Handle<ModListHeader>::New(manager);
						header->SetBounds(AABB2(x, headerPos, modsNameColWidth, headerHeight));
						header->text = _Tr("MainScreen", "Mod");
						modsPanel->AddChild(header.GetPointerOrNull());
						x += modsNameColWidth;
					}
					{
						Handle<ModListHeader> header = Handle<ModListHeader>::New(manager);
						header->SetBounds(AABB2(x, headerPos, modsAuthorColWidth, headerHeight));
						header->text = _Tr("MainScreen", "Author");
						modsPanel->AddChild(header.GetPointerOrNull());
						x += modsAuthorColWidth;
					}
					{
						Handle<ModListHeader> header = Handle<ModListHeader>::New(manager);
						header->SetBounds(AABB2(x, headerPos, modsSizeColWidth, headerHeight));
						header->text = _Tr("MainScreen", "Size");
						modsPanel->AddChild(header.GetPointerOrNull());
					}
				}
				{
					Handle<ListView> list = Handle<ListView>::New(manager);
					modsList = list.GetPointerOrNull();
					modsList->SetBounds(
					    AABB2(contentsLeft, listPos, contentsWidth, footerPos - listPos - 44.0F));
					modsPanel->AddChild(modsList);
				}

				AddChild(serverPanel);
				AddChild(demoPanel);
				AddChild(modsPanel);

				// Tab strip
				{
					Handle<SimpleTabStrip> tabStrip = Handle<SimpleTabStrip>::New(manager);
					tabStrip->SetBounds(AABB2(contentsLeft, tabStripPos, contentsWidth, 24.0F));
					AddChild(tabStrip.GetPointerOrNull());
					tabStrip->AddItem(_Tr("MainScreen", "Servers"), serverPanel);
					tabStrip->AddItem(_Tr("MainScreen", "Demos"), demoPanel);
					// Hide the Mods tab while trying a single mod (--try-mod):
					// the mod manager is irrelevant and its enabled set is bypassed.
					if (!helper->IsTryingMod())
						tabStrip->AddItem(_Tr("MainScreen", "Mods"), modsPanel);
					tabStrip->changed = [this](UIElement& s) { OnTabChanged(s); };
				}

				// Coming back from an apply-triggered relaunch — open the Mods tab.
				if (helper->ShouldOpenModsTab()) {
					serverPanel->visible = false;
					demoPanel->visible = false;
					modsPanel->visible = true;
				}
			}

			// Footer buttons (always visible, outside panels)
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("MainScreen", "Quit");
				button->hotKeyText = _Tr("Client", "[Esc]");
				button->SetBounds(
				    AABB2(contentsLeft + contentsWidth - 100.0F, footerPos, 100.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnQuitPressed(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("MainScreen", "Credits");
				button->SetBounds(
				    AABB2(contentsLeft + contentsWidth - 202.0F, footerPos, 100.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnCreditsPressed(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("MainScreen", "Setup");
				button->SetBounds(
				    AABB2(contentsLeft + contentsWidth - 314.0F, footerPos, 110.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnSetupPressed(s); };
				AddChild(button.GetPointerOrNull());
			}

			LoadServerList();
			LoadDemoList();

			if (helper->ShouldOpenModsTab())
				LoadModList();
		}

		void MainScreenMainMenu::LoadServerList() {
			if (loading)
				return;
			loaded = false;
			loading = true;
			serverList->SetModel(Handle<ListViewModel>::New().GetPointerOrNull()); // empty
			errorView->visible = false;
			loadingView->visible = true;
			helper->StartQuery();
		}

		void MainScreenMainMenu::LoadDemoList() {
			std::vector<std::string> demos = helper->GetDemoList();
			// Reverse so newest demos appear first
			std::vector<std::string> reversed(demos.rbegin(), demos.rend());
			Handle<DemoListModel> model = Handle<DemoListModel>::New(
			    &GetManager(), helper, std::move(reversed), demoDateColWidth, demoModeColWidth,
			    demoMapColWidth, demoSizeColWidth, demoContentsWidth);
			demoList->SetModel(model.GetPointerOrNull());
			model->itemActivated = [this](const std::string& f) { DemoListItemActivated(f); };
			model->itemDoubleClicked = [this](const std::string& f) {
				DemoListItemDoubleClicked(f);
			};
			currentDemoListModel = model;
		}

		void MainScreenMainMenu::OnTabChanged(UIElement&) {
			// Refresh demo list when switching to demos tab
			if (demoPanel->visible)
				LoadDemoList();
			if (modsPanel->visible)
				LoadModList();
		}

		void MainScreenMainMenu::LoadModList() {
			std::vector<std::string> names = modsHelper->GetModNames();
			std::vector<std::string> enabled = modsHelper->GetEnabledMods();

			// Displayed rows keep a fixed order: every mod on disk in its natural
			// order, then any enabled mod that no longer exists (orphaned entries,
			// shown so they can be removed). Toggling only changes a row's apply
			// number and colour, never its position.
			std::vector<std::string> list;
			std::vector<int> orders;
			std::vector<bool> exists;

			for (const std::string& name : names) {
				int idx = EnabledIndex(enabled, name);
				list.push_back(name);
				orders.push_back(idx + 1); // 0 when disabled (idx == -1)
				exists.push_back(true);
			}
			// Orphaned enabled mods (enabled but no longer on disk).
			for (size_t i = 0; i < enabled.size(); i++) {
				bool onDisk = false;
				for (const std::string& name : names) {
					if (name == enabled[i]) {
						onDisk = true;
						break;
					}
				}
				if (!onDisk) {
					list.push_back(enabled[i]);
					orders.push_back(static_cast<int>(i) + 1);
					exists.push_back(false);
				}
			}

			Handle<ModListModel> model = Handle<ModListModel>::New(
			    &GetManager(), modsHelper.GetPointerOrNull(), std::move(list), std::move(orders),
			    std::move(exists), modsCheckColWidth, modsOrderColWidth, modsTagColWidth,
			    modsNameColWidth, modsAuthorColWidth, modsSizeColWidth);
			modsList->SetModel(model.GetPointerOrNull());
			model->itemActivated = [this](const std::string& name) { OnModToggle(name); };
			UpdateModsStatus();
		}

		int MainScreenMainMenu::EnabledIndex(const std::vector<std::string>& enabled,
		                                     const std::string& name) {
			for (size_t i = 0; i < enabled.size(); i++) {
				if (enabled[i] == name)
					return static_cast<int>(i);
			}
			return -1;
		}

		void MainScreenMainMenu::UpdateModsStatus() {
			if (modsApplyButton != nullptr) {
				// Toggles already persist; Apply only restarts to make pending
				// changes live, so it's actionable only when something changed.
				modsApplyButton->enable = modsDirty && !modsDownloading;
			}
			if (modsDownloading) {
				int done = modsHelper->GetRefreshDone();
				int total = modsHelper->GetRefreshTotal();
				std::string item = modsHelper->GetRefreshCurrentItem();
				if (total <= 0) {
					modsStatusLabel->text = _Tr("MainScreen", "Fetching mod list...");
					modsProgressBar->fraction = 0.0F;
				} else {
					std::string label = std::to_string(done) + " / " + std::to_string(total);
					if (item.size() > 0)
						label += " — " + item;
					modsStatusLabel->text = label;
					modsProgressBar->fraction =
					    static_cast<float>(done) / static_cast<float>(total);
				}
				modsProgressBar->visible = true;
				return;
			}
			modsProgressBar->visible = false;
			std::string msg = modsHelper->GetRefreshMessage();
			if (msg.size() > 0) {
				modsStatusLabel->text = msg;
				return;
			}
			if (modsDirty) {
				modsStatusLabel->text =
				    _Tr("MainScreen", "Press 'Apply changes' to restart and apply your mods.");
				return;
			}
			modsStatusLabel->text = "";
		}

		void MainScreenMainMenu::OnDownloadModsPressed(UIElement&) {
			if (modsDownloading)
				return;
			std::string body =
			    _Tr("MainScreen", "Download the official ZeroSpades mod pack?") + "\n\n";
			body +=
			    _Tr("MainScreen", "These are mods reviewed and hosted by the ZeroSpades team.") +
			    "\n\n";
			body += _Tr("MainScreen", "Source: ") + modsHelper->GetIndexUrl() + "\n";
			body += _Tr("MainScreen", "Files will be saved into your Mods/ folder, overwriting "
			                          "any existing copies.");
			Handle<ConfirmScreen> cs = Handle<ConfirmScreen>::New(
			    this, body, std::min(500.0F, GetManager().screenHeight - 100.0F));
			cs->closed = [this](UIElement& s) { OnDownloadConfirmed(s); };
			cs->Run();
		}

		void MainScreenMainMenu::OnDownloadConfirmed(UIElement& sender) {
			ConfirmScreen* cs = dynamic_cast<ConfirmScreen*>(&sender);
			if (cs == nullptr || !cs->GetResult())
				return;
			if (modsDownloading)
				return;
			modsDownloading = true;
			modsHelper->StartRefresh();
			UpdateModsStatus();
		}

		// Apply = restart now so the enabled set is mounted. Toggles are already
		// saved, so this just relaunches straight back into the Mods tab.
		void MainScreenMainMenu::OnApplyModsPressed(UIElement&) {
			if (modsDownloading)
				return;
			helper->RelaunchForMods();
		}

		void MainScreenMainMenu::OnResetModsPressed(UIElement&) {
			Handle<ConfirmScreen> cs = Handle<ConfirmScreen>::New(
			    this, _Tr("MainScreen", "Disable all mods? This takes effect after a restart."));
			cs->closed = [this](UIElement& s) { OnResetConfirmed(s); };
			cs->Run();
		}

		void MainScreenMainMenu::OnResetConfirmed(UIElement& sender) {
			ConfirmScreen* cs = dynamic_cast<ConfirmScreen*>(&sender);
			if (cs == nullptr || !cs->GetResult())
				return;
			modsHelper->ClearEnabledMods();
			modsDirty = true;
			// Refresh the rows to clear every checkbox, not just the status line.
			LoadModList();
		}

		// Clicking a row toggles the mod in the enabled set. The change is saved
		// immediately and takes effect the next time the game starts.
		void MainScreenMainMenu::OnModToggle(const std::string& modName) {
			if (modsDownloading)
				return;
			std::vector<std::string> enabled = modsHelper->GetEnabledMods();
			if (EnabledIndex(enabled, modName) >= 0)
				modsHelper->DisableMod(modName);
			else
				modsHelper->EnableMod(modName);
			modsDirty = true;
			LoadModList();
		}

		void MainScreenMainMenu::CheckModsRefresh() {
			if (!modsDownloading)
				return;
			if (modsHelper->PollRefreshState()) {
				modsDownloading = false;
				LoadModList();
				// A non-empty refresh message means the download failed. The
				// status label keeps it (sticky), but also raise a loud alert so
				// a failure can never be mistaken for "nothing happened".
				std::string err = modsHelper->GetRefreshMessage();
				if (!err.empty()) {
					Handle<AlertScreen> al = Handle<AlertScreen>::New(
					    this, _Tr("MainScreen", "Mod download failed:") + "\n\n" + err);
					al->Run();
				}
				return;
			}
			UpdateModsStatus();
		}

		void MainScreenMainMenu::DemoListItemActivated(const std::string& filename) {
			selectedDemoPath = filename;
			demoNameField->SetText(StripDemoPath(filename));
		}

		void MainScreenMainMenu::DemoListItemDoubleClicked(const std::string& filename) {
			selectedDemoPath = filename;
			demoNameField->SetText(StripDemoPath(filename));
			PlaySelectedDemo();
		}

		void MainScreenMainMenu::PlaySelectedDemo() {
			if (selectedDemoPath.empty())
				return;
			std::string msg = helper->PlayDemo(selectedDemoPath);
			if (msg.size() > 0) {
				Handle<AlertScreen> al = Handle<AlertScreen>::New(this, msg);
				al->Run();
			}
		}

		void MainScreenMainMenu::OnPlayDemoPressed(UIElement&) { PlaySelectedDemo(); }

		void MainScreenMainMenu::OnDeleteDemoPressed(UIElement&) {
			if (selectedDemoPath.empty())
				return;
			std::string name = StripDemoPath(selectedDemoPath);
			Handle<ConfirmScreen> confirm = Handle<ConfirmScreen>::New(
			    this, _Tr("MainScreen",
			              "Are you sure you want to delete '{0}'?\nThis action cannot be undone.",
			              name));
			confirm->closed = [this](UIElement& s) { OnDeleteConfirmClosed(s); };
			confirm->Run();
		}

		void MainScreenMainMenu::OnDeleteConfirmClosed(UIElement& sender) {
			ConfirmScreen* confirm = dynamic_cast<ConfirmScreen*>(&sender);
			if (!confirm || !confirm->GetResult())
				return;
			if (!helper->DeleteDemo(selectedDemoPath))
				return;
			selectedDemoPath = "";
			demoNameField->SetText("");
			LoadDemoList();
		}

		void MainScreenMainMenu::OnRenameDemoPressed(UIElement&) {
			if (selectedDemoPath.empty())
				return;
			Handle<RenameScreen> rename =
			    Handle<RenameScreen>::New(this, StripDemoPath(selectedDemoPath));
			rename->closed = [this](UIElement& s) { OnRenameScreenClosed(s); };
			rename->Run();
		}

		void MainScreenMainMenu::OnRenameScreenClosed(UIElement& sender) {
			RenameScreen* rename = dynamic_cast<RenameScreen*>(&sender);
			if (!rename || !rename->result)
				return;
			std::string newName = rename->newName;
			if (newName.size() >= 4 && newName.substr(newName.size() - 4) == ".dem")
				newName = newName.substr(0, newName.size() - 4);
			if (newName.empty())
				return;
			std::string newPath = "Demos/" + newName + ".dem";
			if (!helper->RenameDemo(selectedDemoPath, newPath)) {
				Handle<AlertScreen> al =
				    Handle<AlertScreen>::New(this, _Tr("MainScreen", "Failed to rename demo."));
				al->Run();
				return;
			}
			selectedDemoPath = newPath;
			demoNameField->SetText(StripDemoPath(newPath));
			LoadDemoList();
		}

		void MainScreenMainMenu::ServerListItemActivated(MainScreenServerItem& item) {
			addressField->SetText(item.GetAddress());
			cg_lastQuickConnectHost = addressField->GetText();
			selectedMapName = item.GetMapName();
			if (item.GetProtocol() == "0.75") {
				SetProtocolVersion(3);
			} else if (item.GetProtocol() == "0.76") {
				SetProtocolVersion(4);
			}
			addressField->SelectAll();
		}

		void MainScreenMainMenu::ServerListItemDoubleClicked(MainScreenServerItem& item) {
			ServerListItemActivated(item);

			// Double-click to connect
			Connect();
		}

		void MainScreenMainMenu::ServerListItemRightClicked(MainScreenServerItem& item) {
			helper->SetServerFavorite(item.GetAddress(), !item.IsFavorite());
			UpdateServerList();
		}

		void MainScreenMainMenu::SortServerList(int keyId) {
			int sort = cg_serverlistSort;
			if ((sort & 0xFFF) == keyId) {
				sort ^= 0x4000;
			} else {
				sort = keyId;
			}
			cg_serverlistSort = sort;
			UpdateServerList();
		}

		void MainScreenMainMenu::UpdateServerList(bool refresh) {
			std::string key = "";
			switch (static_cast<int>(cg_serverlistSort) & 0xFFF) {
				case 0: key = "Ping"; break;
				case 1: key = "NumPlayers"; break;
				case 2: key = "Name"; break;
				case 3: key = "MapName"; break;
				case 4: key = "GameMode"; break;
				case 5: key = "Protocol"; break;
				case 6: key = "Country"; break;
			}
			std::vector<Handle<MainScreenServerItem>> list =
			    helper->GetServerList(key, (static_cast<int>(cg_serverlistSort) & 0x4000) != 0);
			if (list.empty() || loading) {
				serverList->SetModel(Handle<ListViewModel>::New().GetPointerOrNull()); // empty
				return;
			}

			// filter the server list
			bool filterProtocol3 = filterProtocol3Button->toggled;
			bool filterProtocol4 = filterProtocol4Button->toggled;
			bool filterFull = filterFullButton->toggled;
			std::string filterText = filterField->GetText();
			std::vector<Handle<MainScreenServerItem>> list2;
			for (const Handle<MainScreenServerItem>& item : list) {
				if (filterProtocol3 && (item->GetProtocol() != "0.75"))
					continue;
				if (filterProtocol4 && (item->GetProtocol() != "0.76"))
					continue;
				if (filterFull && (item->GetNumPlayers() >= item->GetMaxPlayers()))
					continue;
				if (filterText.size() > 0) {
					if (!(StringContainsCaseInsensitive(item->GetName(), filterText) ||
					      StringContainsCaseInsensitive(item->GetMapName(), filterText) ||
					      StringContainsCaseInsensitive(item->GetGameMode(), filterText))) {
						continue;
					}
				}
				list2.push_back(item);
			}

			// If we are updating the list in real time, try not to replace the
			// model
			if (currentServerListModel && !refresh &&
			    currentServerListModel->list.size() == list2.size()) {
				currentServerListModel->ReplaceList(std::move(list2));
				return;
			}

			Handle<ServerListModel> model =
			    Handle<ServerListModel>::New(&GetManager(), helper, std::move(list2));
			serverList->SetModel(model.GetPointerOrNull());
			model->itemActivated = [this](MainScreenServerItem& i) { ServerListItemActivated(i); };
			model->itemDoubleClicked = [this](MainScreenServerItem& i) {
				ServerListItemDoubleClicked(i);
			};
			model->itemRightClicked = [this](MainScreenServerItem& i) {
				ServerListItemRightClicked(i);
			};

			currentServerListModel = model;

			if (refresh)
				serverList->ScrollToTop();
		}

		void MainScreenMainMenu::CheckServerList() {
			if (helper->PollServerListState()) {
				std::vector<Handle<MainScreenServerItem>> list = helper->GetServerList("", false);
				if (list.empty()) {
					// failed.
					// FIXME: show error message?
					loaded = false;
					loading = false;
					errorView->visible = true;
					loadingView->visible = false;
					serverList->SetModel(Handle<ListViewModel>::New().GetPointerOrNull()); // empty
					return;
				}
				loading = false;
				loaded = true;
				errorView->visible = false;
				loadingView->visible = false;
				UpdateServerList();
			}

			if ((static_cast<int>(cg_serverlistSort) & 0xfff) == 0 && loaded) {
				// Ping (RTT) is updated in real-time
				if (serverListUpdateTimer == 0) {
					UpdateServerList(false);
					serverListUpdateTimer = 5;
				}
				--serverListUpdateTimer;
			}
		}

		void MainScreenMainMenu::OnAddressChanged(UIElement&) {
			cg_lastQuickConnectHost = addressField->GetText();
			selectedMapName = "";
		}

		void MainScreenMainMenu::SetProtocolVersion(int ver) {
			protocol3Button->toggled = (ver == 3);
			protocol4Button->toggled = (ver == 4);
			cg_protocolVersion = ver;
		}

		void MainScreenMainMenu::OnQuitPressed(UIElement&) { ui->shouldExit = true; }

		void MainScreenMainMenu::OnCreditsPressed(UIElement&) {
			Handle<AlertScreen> al = Handle<AlertScreen>::New(
			    this, helper->GetCredits(), std::min(500.0F, GetManager().screenHeight - 100.0F));
			al->Run();
		}

		void MainScreenMainMenu::OnSetupPressed(UIElement&) {
			PreferenceViewOptions opt;
			Handle<PreferenceView> al =
			    Handle<PreferenceView>::New(this, opt, &ui->GetFontManager());
			al->Run();
		}

		void MainScreenMainMenu::Connect() {
			// Pass selectedMapName if known (server clicked in list); otherwise pass empty
			// and let ConnectServer resolve it from the cached server list on the C++ side.
			std::string msg = helper->ConnectServer(addressField->GetText(),
			                                        static_cast<int>(cg_protocolVersion),
			                                        selectedMapName);
			if (msg.size() > 0) {
				Handle<AlertScreen> al = Handle<AlertScreen>::New(this, msg);
				al->Run();
			}
		}

		void MainScreenMainMenu::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Enter") {
				if (demoPanel->visible) {
					PlaySelectedDemo();
				} else {
					Connect();
				}
			} else if (IsEnabled() && key == "Escape") {
				ui->shouldExit = true;
			} else {
				UIElement::HotKey(key);
			}
		}

		void MainScreenMainMenu::Render() {
			CheckServerList();
			CheckModsRefresh();
			UIElement::Render();

			// check for client error message.
			if (IsEnabled()) {
				std::string msg = helper->GetPendingErrorMessage();
				if (msg.size() > 0) {
					// try to make the "disconnected" message more friendly.
					std::string findStr = "Disconnected:";
					size_t ind1pos = msg.find(findStr);
					if (ind1pos != std::string::npos) {
						int ind1 = static_cast<int>(ind1pos);
						size_t ind2pos = msg.find('\n', ind1);
						int ind2 = (ind2pos == std::string::npos) ? static_cast<int>(msg.size())
						                                          : static_cast<int>(ind2pos);
						ind1 += static_cast<int>(findStr.size());
						msg = msg.substr(ind1, ind2 - ind1);
						msg = _Tr("MainScreen",
						          "You were disconnected from the server because of the following "
						          "reason:\n\n{0}",
						          msg);
					}

					// failed to connect.
					Handle<AlertScreen> al = Handle<AlertScreen>::New(this, msg);
					al->Run();
				}
			}
		}
	} // namespace gui
} // namespace spades
