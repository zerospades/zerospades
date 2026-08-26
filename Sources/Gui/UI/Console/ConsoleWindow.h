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
#include <Gui/UI/Widgets/FieldWithHistory.h>

namespace spades {
	namespace gui {
		class ConsoleHelper;
		class ConsoleCommandField;
		namespace ui {
			class TextViewer;
		}

		/** The system console window: a scrollback view plus a command field. */
		class ConsoleWindow : public ui::UIElement {
			ConsoleHelper* helper; // weak
			// Owned here; shared with the command field, which keeps a weak pointer.
			// Declared before `field` so it outlives it.
			std::vector<ui::CommandHistoryItem> history;
			ConsoleCommandField* field; // weak; owned as a child
			ui::TextViewer* viewer;     // weak; owned as a child

		public:
			ConsoleWindow(ConsoleHelper* helper, ui::UIManager* manager);

			void FocusField();
			void AddLine(const std::string& line);

			void HotKey(const std::string& key) override;
		};
	} // namespace gui
} // namespace spades
