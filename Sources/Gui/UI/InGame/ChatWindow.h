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

#include <functional>
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/FieldWithHistory.h>

namespace spades {
	namespace gui {
		namespace ui {
			class TextViewer;
		}
	}
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
			/** Called when Tab is pressed while no "/cvar" completion applies. */
			std::function<void()> tabPressed;

			CommandField(gui::ui::UIManager* manager,
						 std::vector<gui::ui::CommandHistoryItem>* history);

			void OnChanged() override;
			void KeyDown(const std::string& key) override;
		};

		/** How a `ClientChatWindow` is laid out. */
		enum class ChatWindowMode {
			/** The composer alone, centered near the bottom of the screen. */
			Centered,
			/** The composer below a scrollable chat history, at the bottom left. */
			Docked,
			/** Only the chat history, without input, shown while the peek key is held. */
			Peek
		};

		/** The floating chat composer (global/team) shown while playing. */
		class ClientChatWindow : public gui::ui::UIElement {
			ClientUI* ui;             // weak
			ClientUIHelper* helper;   // weak
			bool isTeamChat;
			ChatWindowMode mode;

			gui::ui::TextViewer* viewer = nullptr; // weak; owned as a child

			// Frame of the docked and peek views, relative to this window
			float frameX = 0.0F;
			float frameY = 0.0F;
			float frameW = 0.0F;
			float frameH = 0.0F;
			float separatorY = 0.0F;
			float headerSeparatorY = 0.0F;

			void BuildPanel();
			void BuildHistory(float x, float y, float w, float h, float rowHeight,
			                  bool interactive);

			void OnSetGlobal(gui::ui::UIElement& sender);
			void OnSetTeam(gui::ui::UIElement& sender);
			void OnFieldChanged(gui::ui::UIElement& sender);
			void OnCancel(gui::ui::UIElement& sender);
			bool CheckAndSetConfigVariable();
			void OnSay(gui::ui::UIElement& sender);

		protected:
			virtual void Close();

		public:
			// weak; owned as children. Null when the mode has no such control.
			CommandField* field = nullptr;
			gui::ui::Button* sayButton = nullptr;
			gui::ui::SimpleButton* teamButton = nullptr;
			gui::ui::SimpleButton* globalButton = nullptr;

			ClientChatWindow(ClientUI* ui, bool isTeamChat,
			                 ChatWindowMode mode = ChatWindowMode::Centered);

			void UpdateState();

			/** Appends a line to the history of the docked and peek views. */
			void Record(const std::string& text, Vector4 color);

			/** Scrolls the history; positive moves down, like the mouse wheel. */
			void ScrollBy(float delta);

			bool GetIsTeamChat() const { return isTeamChat; }
			void SetIsTeamChat(bool value);

			void HotKey(const std::string& key) override;
			void Render() override;
		};
	} // namespace client
} // namespace spades
