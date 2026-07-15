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

#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/FieldWithHistory.h>

namespace spades {
	namespace client {
		class ClientUI;
		class ClientUIHelper;

		/** Shows a cvar's current value when the user types something like "/cg_foo". */
		class CommandFieldConfigValueView : public gui::ui::UIElement {
			std::vector<std::string> configNames;
			std::vector<std::string> configValues;

		public:
			CommandFieldConfigValueView(gui::ui::UIManager* manager,
			                            std::vector<std::string> configNames);
			void Render() override;
		};

		/** Chat input field with "/cvar" completion and value preview. */
		class CommandField : public gui::ui::FieldWithHistory {
			Handle<CommandFieldConfigValueView> valueView;

		public:
			CommandField(gui::ui::UIManager* manager,
			             std::vector<gui::ui::CommandHistoryItem>* history);

			void OnChanged() override;
			void KeyDown(const std::string& key) override;
		};

		/** The floating chat composer (global/team) shown while playing. */
		class ClientChatWindow : public gui::ui::UIElement {
			ClientUI* ui;             // weak
			ClientUIHelper* helper;   // weak
			bool isTeamChat;

			void OnSetGlobal(gui::ui::UIElement& sender);
			void OnSetTeam(gui::ui::UIElement& sender);
			void OnFieldChanged(gui::ui::UIElement& sender);
			void OnCancel(gui::ui::UIElement& sender);
			bool CheckAndSetConfigVariable();
			void OnSay(gui::ui::UIElement& sender);

		protected:
			virtual void Close();

		public:
			CommandField* field;                 // weak; owned as a child
			gui::ui::Button* sayButton;          // weak
			gui::ui::SimpleButton* teamButton;   // weak
			gui::ui::SimpleButton* globalButton; // weak

			ClientChatWindow(ClientUI* ui, bool isTeamChat);

			void UpdateState();

			bool GetIsTeamChat() const { return isTeamChat; }
			void SetIsTeamChat(bool value);

			void HotKey(const std::string& key) override;
		};
	} // namespace client
} // namespace spades
