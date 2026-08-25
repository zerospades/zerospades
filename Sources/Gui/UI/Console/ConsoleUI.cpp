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

#include "ConsoleUI.h"
#include "ConsoleWindow.h"
#include <Client/Fonts.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		ConsoleUI::ConsoleUI(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
		                     client::FontManager* fontManager, ConsoleHelper* helper) {
			manager = Handle<ui::UIManager>::New(renderer, audioDevice);
			manager->GetRootElement().SetFont(&fontManager->GetGuiFont());

			Handle<ConsoleWindow> cw =
			    Handle<ConsoleWindow>::New(helper, manager.GetPointerOrNull());
			console = cw.GetPointerOrNull();
			console->SetBounds(manager->GetRootElement().GetBounds());
			manager->GetRootElement().AddChild(console);
		}

		ConsoleUI::~ConsoleUI() {}

		void ConsoleUI::MouseEvent(float x, float y) { manager->MouseEvent(x, y); }
		void ConsoleUI::WheelEvent(float x, float y) { manager->WheelEvent(x, y); }

		void ConsoleUI::KeyEvent(const std::string& key, bool down) {
			if (key == "Escape") {
				active = false;
				return;
			}
			manager->KeyEvent(key, down);
		}

		void ConsoleUI::TextInputEvent(const std::string& text) { manager->TextInputEvent(text); }
		void ConsoleUI::TextEditingEvent(const std::string& text, int start, int len) {
			manager->TextEditingEvent(text, start, len);
		}

		bool ConsoleUI::AcceptsTextInput() { return manager->AcceptsTextInput(); }
		AABB2 ConsoleUI::GetTextInputRect() { return manager->GetTextInputRect(); }

		void ConsoleUI::RunFrame(float dt) {
			manager->RunFrame(dt);
			if (active)
				manager->Render();
		}

		void ConsoleUI::Closing() {}

		bool ConsoleUI::ShouldInterceptInput() { return active; }

		void ConsoleUI::ToggleConsole() {
			active = !active;
			if (active)
				console->FocusField();
		}

		void ConsoleUI::AddLine(const std::string& line) { console->AddLine(line); }
	} // namespace gui
} // namespace spades
