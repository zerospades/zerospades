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
#include <cmath>

#include "ChatWindow.h"
#include <Client/ClientUI.h>
#include <Client/ClientUIHelper.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/TextUtils.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/MessageBox.h>
#include <Gui/UI/Widgets/TextViewer.h>

SPADES_SETTING(cg_killfeedHeight);

namespace spades {
	namespace client {
		using gui::ui::Button;
		using gui::ui::CommandHistoryItem;
		using gui::ui::Label;
		using gui::ui::MouseButton;
		using gui::ui::SetColorNP;
		using gui::ui::SimpleButton;
		using gui::ui::StringCommonPrefixLength;
		using gui::ui::TextViewer;
		using gui::ui::UIElement;
		using gui::ui::UIManager;

		namespace {
			/** The color a history line is drawn in: the recorded color, lightened. */
			Vector4 HistoryColor(Vector4 color) {
				color.x = Mix(color.x, 1.0F, 0.5F);
				color.y = Mix(color.y, 1.0F, 0.5F);
				color.z = Mix(color.z, 1.0F, 0.5F);
				color.w = 1.0F;
				return color;
			}
		} // namespace

		// -- CommandFieldConfigValueView --

		CommandFieldConfigValueView::CommandFieldConfigValueView(UIManager* manager,
		                                                         std::vector<std::string> names)
		    : UIElement(manager), configNames(std::move(names)) {
			for (const std::string& n : configNames)
				configValues.push_back(
				    static_cast<std::string>(Settings::ItemHandle(n, nullptr)));
		}

		void CommandFieldConfigValueView::Render() {
			float maxNameLen = 0.0F;
			float maxDescLen = 0.0F;
			client::IFont* font = GetFont();
			if (!font)
				return;
			client::IRenderer& r = GetManager().GetRenderer();
			float rowHeight = 24.0F;

			for (size_t i = 0, len = configNames.size(); i < len; i++) {
				maxNameLen = std::max(maxNameLen, font->Measure(configNames[i]).x);
				maxDescLen = std::max(maxDescLen, font->Measure(configValues[i]).x);
			}

			float w = maxNameLen + maxDescLen + 20.0F;
			float h = static_cast<float>(configNames.size()) * rowHeight + 10.0F;

			Vector2 pos = GetScreenPosition();
			pos.y -= h;
			pos.y += rowHeight - 10.0F;

			// draw background
			SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.9F));
			r.DrawFilledRect(pos.x + 1, pos.y + 1, pos.x + w - 1, pos.y + h - 1);

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.1F));
			r.DrawOutlinedRect(pos.x, pos.y, pos.x + w, pos.y + h);

			// draw cvar list
			Vector4 nameColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.7F);
			Vector4 nameShadow = MakeVector4(0.0F, 0.0F, 0.0F, 0.3F);
			Vector4 descColor = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			Vector4 descShadow = MakeVector4(0.0F, 0.0F, 0.0F, 0.4F);

			for (size_t i = 0, len = configNames.size(); i < len; i++) {
				float y = 8.0F + static_cast<float>(i) * rowHeight;
				Vector2 namePos = pos + MakeVector2(5.0F, y - 2.0F);
				Vector2 descPos = pos + MakeVector2(15.0F + maxNameLen, y - 2.0F);

				font->DrawShadow(configNames[i], namePos, 1.0F, nameColor, nameShadow);
				font->DrawShadow(configValues[i], descPos, 1.0F, descColor, descShadow);
			}
		}

		// -- CommandField --

		CommandField::CommandField(UIManager* manager, std::vector<CommandHistoryItem>* history)
		    : gui::ui::FieldWithHistory(manager, history) {}

		void CommandField::OnChanged() {
			gui::ui::FieldWithHistory::OnChanged();

			if (valueView)
				valueView->SetParent(nullptr);

			const std::string& text = GetText();
			if (text.substr(0, 1) == "/" && text.substr(1, 1) != " ") {
				size_t wsPos = text.find(' ');
				int whitespace =
				    (wsPos == std::string::npos) ? static_cast<int>(text.size()) : static_cast<int>(wsPos);

				std::string input = text.substr(1, whitespace - 1);
				if (input.size() >= 2) {
					std::vector<std::string> names = Settings::GetInstance()->GetAllItemNames();
					std::vector<std::string> filteredNames;
					for (const std::string& n : names) {
						if (StringCommonPrefixLength(input, n) == input.size() &&
						    !Settings::ItemHandle(n, nullptr).IsUnknown()) {
							filteredNames.push_back(n);
							if (filteredNames.size() >= 8)
								break; // too many
						}
					}
					if (filteredNames.size() > 0) {
						valueView = Handle<CommandFieldConfigValueView>::New(
						    &GetManager(), std::move(filteredNames));
						valueView->SetBounds(AABB2(0.0F, -15.0F, 0.0F, 0.0F));
						valueView->SetParent(this);
					}
				}
			}
		}

		void CommandField::KeyDown(const std::string& key) {
			if (key == "Tab") {
				const std::string& text = GetText();
				if (text.substr(0, 1) == "/") {
					if (GetSelectionLength() == 0 && GetSelectionStart() == static_cast<int>(text.size()) &&
						text.find(' ') == std::string::npos) {
						// config variable auto completion
						std::string input = text.substr(1);
						std::vector<std::string> names = Settings::GetInstance()->GetAllItemNames();
						std::string commonPart;
						bool foundOne = false;
						for (const std::string& n : names) {
							if (StringCommonPrefixLength(input, n) == input.size() &&
								!Settings::ItemHandle(n, nullptr).IsUnknown()) {
								if (!foundOne) {
									commonPart = n;
									foundOne = true;
								}

								size_t commonLen = StringCommonPrefixLength(commonPart, n);
								commonPart = commonPart.substr(0, commonLen);
							}
						}

						if (commonPart.size() > input.size()) {
							SetText("/" + commonPart);
							Select(static_cast<int>(GetText().size()), 0);
						}
					}
				} else if (tabPressed) {
					// not a command: Tab switches the recipient
					tabPressed();
				}
			} else {
				gui::ui::FieldWithHistory::KeyDown(key);
			}
		}

		// -- ClientChatWindow --

		ClientChatWindow::ClientChatWindow(ClientUI* ui, bool isTeamChat, ChatWindowMode mode)
		    : UIElement(&ui->GetUIManager()),
		      ui(ui),
		      helper(&ui->GetHelper()),
		      isTeamChat(isTeamChat),
		      mode(mode) {
			UIManager* manager = &GetManager();

			// The history measures text while it is built, which happens before this window
			// is attached to the root and could inherit the font from it.
			SetFont(manager->GetRootElement().GetFont());

			if (mode != ChatWindowMode::Centered) {
				BuildPanel();
				return;
			}

			float sw = manager->screenWidth;
			float sh = manager->screenHeight;

			float winW = sw * 0.7F, winH = 66.0F;
			float winX = (sw - winW) * 0.5F;
			float winY = (sh - winH) - 20.0F;

			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.5F);
				label->SetBounds(AABB2(winX - 8.0F, winY - 8.0F, winW + 16.0F, winH + 16.0F));
				AddChild(label.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Send");
				button->hotKeyText = _Tr("Client", "[Enter]");
				button->SetBounds(AABB2(winX + winW - 244.0F, winY + 36.0F, 120.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnSay(s); };
				AddChild(button.GetPointerOrNull());
				sayButton = button.GetPointerOrNull();
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Cancel");
				button->hotKeyText = _Tr("Client", "[Esc]");
				button->SetBounds(AABB2(winX + winW - 120.0F, winY + 36.0F, 120.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnCancel(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<CommandField> f =
				    Handle<CommandField>::New(manager, &ui->GetChatHistory());
				field = f.GetPointerOrNull();
				field->SetBounds(AABB2(winX, winY, winW, 30.0F));
				field->placeholder = _Tr("Client", "Chat Text");
				field->maxLength = 90;
				field->removeNewlines = true;
				field->changed = [this](UIElement& s) { OnFieldChanged(s); };
				field->tabPressed = [this]() { SetIsTeamChat(!GetIsTeamChat()); };
				AddChild(field);
			}
			{
				Handle<SimpleButton> button = Handle<SimpleButton>::New(manager);
				button->toggle = true;
				button->toggled = (isTeamChat == false);
				button->caption = _Tr("Client", "Global");
				button->SetBounds(AABB2(winX, winY + 36.0F, 70.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnSetGlobal(s); };
				AddChild(button.GetPointerOrNull());
				globalButton = button.GetPointerOrNull();
			}
			{
				Handle<SimpleButton> button = Handle<SimpleButton>::New(manager);
				button->toggle = true;
				button->toggled = (isTeamChat == true);
				button->caption = _Tr("Client", "Team");
				button->SetBounds(AABB2(winX + 70.0F + 1.0F, winY + 36.0F, 70.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnSetTeam(s); };
				AddChild(button.GetPointerOrNull());
				teamButton = button.GetPointerOrNull();
			}

			{
				std::string switchHint = _Tr("Client", "[Tab] Switch");
				IFont* hintFont = GetFont();
				float switchW = hintFont ? std::ceil(hintFont->Measure(switchHint).x) : 100.0F;

				Handle<Label> label = Handle<Label>::New(manager);
				label->text = switchHint;
				label->textColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.5F);
				label->SetBounds(AABB2(winX + 70.0F * 2.0F + 1.0F + 8.0F, winY + 36.0F + 5.0F,
									   switchW + 2.0F, 20.0F));
				AddChild(label.GetPointerOrNull());
			}

			UpdateState();
		}

		void ClientChatWindow::BuildPanel() {
			UIManager* manager = &GetManager();
			const float sw = manager->screenWidth;
			const float sh = manager->screenHeight;
			const bool docked = (mode == ChatWindowMode::Docked);

			// Share of the screen height taken by the history. The peek view is the extended
			// one: it has no composer to make room for, so it shows more of the history.
			const float dockedHeightRatio = 0.45F;
			const float peekHeightRatio = 0.65F;

			const float margin = 8.0F;    // distance from the screen edge, as in the HUD chat
			const float pad = 8.0F;       // space between the frame and its contents
			const float gap = 6.0F;       // space between the parts of the panel
			const float rowH = 30.0F;     // height of the composer row
			const float headerH = 26.0F;  // height of the channel toggles above the history

			float panelW = std::min(sw - margin * 2.0F, Clamp(sw * 0.38F, 520.0F, 760.0F));
			float contentW = panelW - pad * 2.0F;
			float contentX = margin + pad;

			// whole rows only, so the last row of the history is never cut in half
			client::IFont* font = GetFont();
			float rowHeight = 22.0F;
			float composerY = sh - 20.0F - rowH;
			float historyBottom = composerY - gap;

			// stay clear of the killfeed in the top left corner
			float topLimit = std::min(sh * 0.5F, sh * (float)cg_killfeedHeight * 0.01F + 48.0F);
			float chromeAbove = pad + (docked ? headerH + gap : 0.0F);
			float maxHistoryH = std::max(100.0F, historyBottom - chromeAbove - topLimit);
			float wanted = sh * (docked ? dockedHeightRatio : peekHeightRatio);
			float historyH = std::floor(Clamp(wanted, 100.0F, maxHistoryH) / rowHeight) * rowHeight;
			float historyTop = historyBottom - historyH;
			float headerY = historyTop - gap - headerH;

			frameX = margin;
			frameY = (docked ? headerY : historyTop) - pad;
			frameW = panelW;
			frameH = (docked ? (sh - 12.0F) : (historyBottom + pad)) - frameY;
			headerSeparatorY = historyTop - gap * 0.5F;
			separatorY = historyBottom + gap * 0.5F;

			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.8F);
				label->SetBounds(AABB2(frameX, frameY, frameW, frameH));
				AddChild(label.GetPointerOrNull());
			}

			BuildHistory(contentX, historyTop, contentW, historyH, rowHeight, docked);

			if (!docked)
				return;

			// channel toggles
			const float toggleW = 70.0F;
			{
				Handle<SimpleButton> button = Handle<SimpleButton>::New(manager);
				button->toggle = true;
				button->toggled = (isTeamChat == false);
				button->caption = _Tr("Client", "Global");
				button->SetBounds(AABB2(contentX, headerY, toggleW, headerH));
				button->activated = [this](UIElement& s) { OnSetGlobal(s); };
				AddChild(button.GetPointerOrNull());
				globalButton = button.GetPointerOrNull();
			}
			{
				Handle<SimpleButton> button = Handle<SimpleButton>::New(manager);
				button->toggle = true;
				button->toggled = (isTeamChat == true);
				button->caption = _Tr("Client", "Team");
				button->SetBounds(AABB2(contentX + toggleW + 1.0F, headerY, toggleW, headerH));
				button->activated = [this](UIElement& s) { OnSetTeam(s); };
				AddChild(button.GetPointerOrNull());
				teamButton = button.GetPointerOrNull();
			}

			// draw hints
			if (font) {
				const float toggleEnd = contentX + toggleW * 2.0F + 1.0F;
				const float hintGap = 8.0F;

				// right aligned: the keys of the composer
				std::string hint = _Tr("Client", "[Enter] Send  [Esc] Cancel");
				float hintW = std::ceil(font->Measure(hint).x);
				{
					Handle<Label> label = Handle<Label>::New(manager);
					label->text = hint;
					label->textColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.5F);
					label->SetBounds(AABB2(contentX + contentW - hintW, headerY + 4.0F, hintW + 2.0F, 20.0F));
					AddChild(label.GetPointerOrNull());
				}

				// next to the toggles it controls
				std::string switchHint = _Tr("Client", "[Tab] Switch");
				float switchW = std::ceil(font->Measure(switchHint).x);
				{
					Handle<Label> label = Handle<Label>::New(manager);
					label->text = switchHint;
					label->textColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.5F);
					label->SetBounds(AABB2(toggleEnd + hintGap, headerY + 4.0F, switchW + 2.0F, 20.0F));
					AddChild(label.GetPointerOrNull());
				}
			}

			// composer
			{
				Handle<CommandField> f = Handle<CommandField>::New(manager, &ui->GetChatHistory());
				field = f.GetPointerOrNull();
				field->SetBounds(AABB2(contentX, composerY, contentW, rowH));
				field->placeholder = _Tr("Client", "Chat Text");
				field->maxLength = 90;
				field->removeNewlines = true;
				field->changed = [this](UIElement& s) { OnFieldChanged(s); };
				field->tabPressed = [this]() { SetIsTeamChat(!isTeamChat); };
				AddChild(field);
			}

			UpdateState();
		}

		void ClientChatWindow::BuildHistory(float x, float y, float w, float h, float rowHeight,
		                                    bool interactive) {
			Handle<TextViewer> v = Handle<TextViewer>::New(&GetManager());
			AddChild(v.GetPointerOrNull());
			viewer = v.GetPointerOrNull();

			// A click on the history must not take the keyboard from the composer, and the
			// peek view has no mouse at all. Scrolling, links and selecting text do not need
			// the focus; copying is handled in `HotKey`.
			viewer->acceptsFocus = false;
			viewer->SetSelectionAlwaysVisible(interactive);
			viewer->SetScrollBarVisible(interactive);
			viewer->rowHeight = rowHeight;
			viewer->bottomAligned = true;
			viewer->SetBounds(AABB2(x, y, w, h));

			// Links are drawn in both views, but only the docked one has a mouse to click them
			// with. Every link goes through a confirmation dialog.
			viewer->parseLinks = true;
			if (interactive) {
				viewer->linkActivated = [this](const std::string& url) {
					gui::ConfirmOpenLink(this, url);
				};
			}

			// replay the most recent lines; older ones stay in the chat log window
			const size_t maxReplayed = 150;
			const auto& records = ui->chatLogRecords;
			size_t first = records.size() > maxReplayed ? records.size() - maxReplayed : 0;
			for (size_t i = first; i < records.size(); i++)
				viewer->AddLine(records[i].first, false, HistoryColor(records[i].second));
			viewer->Layout();
			viewer->ScrollToEnd();
		}

		void ClientChatWindow::Record(const std::string& text, Vector4 color) {
			if (viewer)
				viewer->AddLine(text, true, HistoryColor(color));
		}

		void ClientChatWindow::ScrollBy(float delta) {
			if (viewer)
				viewer->MouseWheel(delta);
		}

		void ClientChatWindow::UpdateState() {
			if (field && sayButton)
				sayButton->enable = field->GetText().size() > 0;
		}

		void ClientChatWindow::SetIsTeamChat(bool value) {
			if (isTeamChat == value)
				return;
			isTeamChat = value;
			if (teamButton)
				teamButton->toggled = isTeamChat;
			if (globalButton)
				globalButton->toggled = !isTeamChat;
			UpdateState();
		}

		void ClientChatWindow::OnSetGlobal(UIElement&) { SetIsTeamChat(false); }
		void ClientChatWindow::OnSetTeam(UIElement&) { SetIsTeamChat(true); }
		void ClientChatWindow::OnFieldChanged(UIElement&) { UpdateState(); }

		void ClientChatWindow::Close() { ui->SetActiveUI(nullptr); }

		void ClientChatWindow::OnCancel(UIElement&) {
			field->Cancelled();
			Close();
		}

		bool ClientChatWindow::CheckAndSetConfigVariable() {
			std::string text = field->GetText();
			if (text.substr(0, 1) != "/")
				return false;
			size_t idxPos = text.find(' ');
			int idx = (idxPos == std::string::npos) ? -1 : static_cast<int>(idxPos);
			if (idx < 2)
				return false;

			// find variable
			std::string varname = text.substr(1, idx - 1);
			std::vector<std::string> vars = Settings::GetInstance()->GetAllItemNames();

			for (const std::string& v : vars) {
				if (v.size() == varname.size() &&
				    StringCommonPrefixLength(v, varname) == v.size()) {
					// match
					std::string val = text.substr(idx + 1);
					Settings::ItemHandle item(v, nullptr);
					item = val;
					return true;
				}
			}

			return false;
		}

		void ClientChatWindow::OnSay(UIElement&) {
			field->CommandSent();
			if (!CheckAndSetConfigVariable()) {
				if (isTeamChat)
					helper->SayTeam(field->GetText());
				else
					helper->SayGlobal(field->GetText());
			}

			Close();
		}

		void ClientChatWindow::Render() {
			UIElement::Render();

			if (mode == ChatWindowMode::Centered)
				return;

			UIManager& manager = GetManager();

			// A dialog opened from a link disables this window, which drops the focus. Give
			// it back to the composer once the window is usable again.
			if (mode == ChatWindowMode::Docked && field && IsEnabled() &&
			    manager.GetActiveElement() == nullptr)
				manager.SetActiveElement(field);

			client::IRenderer& r = manager.GetRenderer();
			Vector2 pos = GetScreenPosition();

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));
			r.DrawOutlinedRect(pos.x + frameX, pos.y + frameY, pos.x + frameX + frameW,
			                   pos.y + frameY + frameH);
			if (mode == ChatWindowMode::Docked) {
				r.DrawImage(nullptr, AABB2(pos.x + frameX + 8.0F, pos.y + headerSeparatorY,
				                           frameW - 16.0F, 1.0F));
				r.DrawImage(nullptr, AABB2(pos.x + frameX + 8.0F, pos.y + separatorY, frameW - 16.0F, 1.0F));
			}
		}

		void ClientChatWindow::HotKey(const std::string& key) {
			if (!field) {
				UIElement::HotKey(key);
				return;
			}

			// The composer keeps the keyboard focus, so its own Ctrl+C has already run and
			// copied nothing when nothing is selected in it. Copy the history selection then.
			UIManager& manager = GetManager();
			if (IsEnabled() && viewer && (manager.isControlPressed || manager.isMetaPressed) &&
			    key == "C" && viewer->HasSelection() && field->GetSelectionLength() == 0) {
				manager.Copy(viewer->GetSelectedText());
				return;
			}

			if (IsEnabled() && key == "Escape") {
				OnCancel(*this);
			} else if (IsEnabled() && key == "Enter") {
				if (field->GetText().size() > 0)
					OnSay(*this);
			} else {
				UIElement::HotKey(key);
			}
		}
	} // namespace client
} // namespace spades
