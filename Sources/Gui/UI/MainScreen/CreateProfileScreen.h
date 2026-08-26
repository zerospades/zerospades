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

#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Field.h>

namespace spades {
	namespace client {
		class FontManager;
	}
	namespace gui {
		/** First-run dialog prompting the new player to choose their name. */
		class CreateProfileScreen : public ui::UIElement {
			float contentsTop, contentsHeight;

			ui::UIElement* owner; // weak
			ui::Field* nameField; // weak; owned as a child
			ui::Button* okButton; // weak; owned as a child

			void OnOkPressed(ui::UIElement& sender);
			void OnChooseLater(ui::UIElement& sender);
			void OnNameChanged(ui::UIElement& sender);

		public:
			ui::EventHandler closed;

			CreateProfileScreen(ui::UIElement* owner, client::FontManager* fontManager);

			void Close();
			void Run();

			void HotKey(const std::string& key) override;
			void Render() override;
		};
	} // namespace gui
} // namespace spades
