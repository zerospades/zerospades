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

namespace spades {
	namespace client {
		using gui::ui::Button;
		using gui::ui::CommandHistoryItem;
		using gui::ui::Label;
		using gui::ui::MouseButton;
		using gui::ui::SetColorNP;
		using gui::ui::SimpleButton;
		using gui::ui::StringCommonPrefixLength;
		using gui::ui::UIElement;
		using gui::ui::UIManager;

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
			SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.75F));
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
				if (GetSelectionLength() == 0 && GetSelectionStart() == static_cast<int>(text.size()) &&
				    text.substr(0, 1) == "/" && text.find(' ') == std::string::npos) {
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
			} else {
				gui::ui::FieldWithHistory::KeyDown(key);
			}
		}

		// -- ClientChatWindow --

		ClientChatWindow::ClientChatWindow(ClientUI* ui, bool isTeamChat)
		    : UIElement(&ui->GetUIManager()),
		      ui(ui),
		      helper(&ui->GetHelper()),
		      isTeamChat(isTeamChat) {
			UIManager* manager = &GetManager();
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

			UpdateState();
		}

		void ClientChatWindow::UpdateState() {
			sayButton->enable = field->GetText().size() > 0;
		}

		void ClientChatWindow::SetIsTeamChat(bool value) {
			if (isTeamChat == value)
				return;
			isTeamChat = value;
			teamButton->toggled = isTeamChat;
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

		void ClientChatWindow::HotKey(const std::string& key) {
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
