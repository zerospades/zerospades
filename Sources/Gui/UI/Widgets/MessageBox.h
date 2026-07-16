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

#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		/**
		 * A modal dialog with a text body and a row of buttons. While shown it
		 * disables its owner; `resultIndex` is the pressed button (or -1).
		 */
		class MessageBoxScreen : public ui::UIElement {
			float contentsTop, contentsHeight;

			ui::UIElement* owner; // weak

			void OnClosed();

		public:
			ui::EventHandler closed;
			int resultIndex = -1;

			MessageBoxScreen(ui::UIElement* owner, const std::string& text,
			                 const std::vector<std::string>& buttons, float height = 200.0F);

			void EndDialog(int result);
			void Close();
			void Run();
			void Render() override;
		};

		/** A message box with a single OK button. */
		class AlertScreen : public MessageBoxScreen {
		public:
			AlertScreen(ui::UIElement* owner, const std::string& text, float height = 200.0F);
			void HotKey(const std::string& key) override;
		};

		/** A message box with OK / Cancel buttons; `GetResult()` is true for OK. */
		class ConfirmScreen : public MessageBoxScreen {
		public:
			ConfirmScreen(ui::UIElement* owner, const std::string& text, float height = 200.0F);

			bool GetResult() const { return resultIndex == 0; }

			void HotKey(const std::string& key) override;
		};
	} // namespace gui
} // namespace spades
