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

#include <Core/Math.h>
#include <Gui/UI/InGame/ChatWindow.h>
#include <Gui/UI/Widgets/TextViewer.h>

namespace spades {
	namespace client {
		class ClientUI;
		class ClientUIHelper;
		class ChatLogWindow;

		/** The composer spawned from within the chat log; closes back to the log. */
		class ChatLogSayWindow : public ClientChatWindow {
			ChatLogWindow* owner; // weak

		protected:
			void Close() override;

		public:
			ChatLogSayWindow(ChatLogWindow* owner, bool isTeamChat);
		};

		/** The scrollable chat history overlay with Say Global/Team buttons. */
		class ChatLogWindow : public gui::ui::UIElement {
			friend class ChatLogSayWindow;

			float contentsTop = 0.0F;
			float contentsHeight = 0.0F;

			ClientUI* ui;           // weak
			ClientUIHelper* helper; // weak

			gui::ui::TextViewer* viewer;    // weak; owned as a child
			ChatLogSayWindow* sayWindow = nullptr; // weak; owned as a child while open

			gui::ui::UIElement* sayButton1; // weak
			gui::ui::UIElement* sayButton2; // weak

			void OnOkPressed(gui::ui::UIElement& sender);
			void OnChat(bool isTeam);
			void OnTeamChat(gui::ui::UIElement& sender);
			void OnGlobalChat(gui::ui::UIElement& sender);
			void Close();

		public:
			ChatLogWindow(ClientUI* ui);

			void ScrollToEnd();
			void UpdateState(bool chatEnabled);
			void SayWindowClosed();

			void HotKey(const std::string& key) override;
			void Record(const std::string& text, Vector4 color);
			void Render() override;
		};
	} // namespace client
} // namespace spades
