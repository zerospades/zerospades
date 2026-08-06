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

#include <algorithm>

#include "ConsoleUI.h"
#include "ConsoleWindow.h"
#include <Client/Fonts.h>
#include <Core/Settings.h>
#include <Gui/UI/Framework/UIManager.h>

SPADES_SETTING(cl_consoleScrollbackLines); // defined in Gui/UI/Console/ConsoleWindow.cpp

namespace spades {
	namespace gui {
		ConsoleUI::ConsoleUI(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
		                     client::FontManager* fontManager, ConsoleHelper* helper)
		    : helper(helper) {
			manager = Handle<ui::UIManager>::New(renderer, audioDevice);
			manager->GetRootElement().SetFont(&fontManager->GetGuiFont());
			manager->screenSizeChanged = [this] { ReloadScreen(); };

			Init();
		}

		ConsoleUI::~ConsoleUI() {}

		void ConsoleUI::Init() {
			Handle<ConsoleWindow> cw =
			    Handle<ConsoleWindow>::New(helper, manager.GetPointerOrNull(), &commandHistory);
			console = cw.GetPointerOrNull();
			console->SetBounds(manager->GetRootElement().GetBounds());
			manager->GetRootElement().AddChild(console);
		}

		void ConsoleUI::ReloadScreen() {
			ui::UIElement& root = manager->GetRootElement();
			root.RemoveChild(console);

			// Rebuild through the constructor so the new layout is exactly what a
			// console opened at this extent would have looked like, then replay the
			// scrollback — it lives nowhere but in the window we just dropped.
			Init();
			for (const std::string& line : scrollback)
				console->AddLine(line);

			if (active)
				console->FocusField();
		}

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

		void ConsoleUI::AddLine(const std::string& line) {
			// Mirrors the viewer's own cap so a rebuild reproduces what was on screen
			// rather than resurrecting lines it had already dropped.
			scrollback.push_back(line);
			size_t maxLines =
			    static_cast<size_t>(std::max(static_cast<int>(cl_consoleScrollbackLines), 1));
			if (scrollback.size() > maxLines)
				scrollback.erase(scrollback.begin(),
				                 scrollback.begin() + (scrollback.size() - maxLines));

			console->AddLine(line);
		}
	} // namespace gui
} // namespace spades
