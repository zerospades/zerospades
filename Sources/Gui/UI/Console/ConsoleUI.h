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

#include <Core/Math.h>
#include <Core/RefCountedObject.h>
#include <Gui/UI/Widgets/FieldWithHistory.h>

namespace spades {
	namespace client {
		class IRenderer;
		class IAudioDevice;
		class FontManager;
	}
	namespace gui {
		class ConsoleHelper;
		class ConsoleWindow;
		namespace ui {
			class UIManager;
		}

		/**
		 * Implements the system console window.
		 *
		 * `ConsoleUI` is overlaid on the normal rendering. Normally it's invisible,
		 * but it becomes visible when invoked by a hotkey. When it's visible, it also
		 * intercepts all inputs.
		 */
		class ConsoleUI {
			Handle<ui::UIManager> manager;
			bool active = false;
			ConsoleWindow* console; // weak; owned by the UI tree
			ConsoleHelper* helper;  // weak; outlives this UI, needed to rebuild

			// The window bakes the screen extent into its layout, so a resize rebuilds
			// it. Both of these outlive the window so the rebuilt one can be restored.
			std::vector<ui::CommandHistoryItem> commandHistory;
			std::vector<std::string> scrollback;

			void Init();
			void ReloadScreen();

		public:
			ConsoleUI(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
			          client::FontManager* fontManager, ConsoleHelper* helper);
			~ConsoleUI();

			void MouseEvent(float x, float y);
			void WheelEvent(float x, float y);
			void KeyEvent(const std::string& key, bool down);
			void TextInputEvent(const std::string& text);
			void TextEditingEvent(const std::string& text, int start, int len);
			bool AcceptsTextInput();
			AABB2 GetTextInputRect();

			void RunFrame(float dt);
			void Closing();

			bool ShouldInterceptInput();
			void ToggleConsole();
			void AddLine(const std::string& line);
		};
	} // namespace gui
} // namespace spades
