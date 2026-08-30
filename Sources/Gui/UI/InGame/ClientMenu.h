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

#include <Gui/UI/Framework/UIElement.h>
#include <Gui/UI/Settings/PreferenceView.h>
#include <Gui/UI/Components/IModalMenu.h>

namespace spades {
	namespace client {
		class ClientUI;
		class ClientUIHelper;

		/** The in-game escape menu (Back to Game / Chat Log / Setup / Disconnect). */
		class ClientMenu : public gui::ui::UIElement, public gui::IModalMenu {
			ClientUI* ui;           // weak
			ClientUIHelper* helper; // weak
			// Lives for the session so reopening Setup lands on the same tab/row.
			Handle<gui::PreferenceViewPersistedState> preferenceState;

			void OnBackToGame(gui::ui::UIElement& sender);
			void OnSetup(gui::ui::UIElement& sender);
			void OnChatLog(gui::ui::UIElement& sender);
			void OnDisconnect(gui::ui::UIElement& sender);

		public:
			ClientMenu(ClientUI* ui);
			void HotKey(const std::string& key) override;

			// IModalMenu implementation (UIElement handles input/drawing)
			bool IsActive() const override;
			void Open() override;
			void Close() override;
			bool KeyEvent(const std::string& key, bool down) override;
			void TextInputEvent(const std::string& text) override;
			bool AcceptsTextInput() const override;
			void Draw() override;
		};
	} // namespace client
} // namespace spades
