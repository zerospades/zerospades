/*
 Copyright (c) 2019 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <map>

#include <Client/Fonts.h>
#include <Core/TMPUtils.h>

#include "ConfigConsoleResponder.h"
#include "ConsoleHelper.h"
#include "ConsoleScreen.h"
#include <Gui/UI/Console/ConsoleUI.h>
#include <Gui/Utils/ConsoleCommand.h>

namespace spades {
	namespace gui {
		namespace {
			bool s_consoleOpen = false;
		}

		ConsoleScreen::ConsoleScreen(Handle<client::IRenderer> renderer,
									 Handle<client::IAudioDevice> audioDevice,
									 Handle<client::FontManager> fontManager, Handle<View> subview)
			: renderer{renderer}, audioDevice{audioDevice}, subview{subview} {
			SPADES_MARK_FUNCTION();

			helper = Handle<ConsoleHelper>::New(this);

			ui = std::make_unique<ConsoleUI>(renderer.GetPointerOrNull(),
											 audioDevice.GetPointerOrNull(),
											 fontManager.GetPointerOrNull(), helper.GetPointerOrNull());
		}

		ConsoleScreen::~ConsoleScreen() {
			SPADES_MARK_FUNCTION();

			s_consoleOpen = false;
			helper->ConsoleScreenDestroyed();
		}

		void ConsoleScreen::MouseEvent(float x, float y) {
			SPADES_MARK_FUNCTION();

			if (ShouldInterceptInput())
				ui->MouseEvent(x, y);
			else
				subview->MouseEvent(x, y);
		}

		void ConsoleScreen::KeyEvent(const std::string& key, bool down) {
			SPADES_MARK_FUNCTION();

			// TODO: Check if "`" is correct
			if ((key == "`" || key == "F1") && down) {
				ToggleConsole();
				consoleToggleKey = key;
				return;
			}

			if (down && !consoleToggleKey.empty())
				consoleToggleKey.clear();

			if (ShouldInterceptInput())
				ui->KeyEvent(key, down);
			else
				subview->KeyEvent(key, down);
		}

		void ConsoleScreen::TextInputEvent(const std::string& ch) {
			SPADES_MARK_FUNCTION();

			if (!consoleToggleKey.empty()) {
				consoleToggleKey.clear();
				return;
			}

			if (ShouldInterceptInput())
				ui->TextInputEvent(ch);
			else
				subview->TextInputEvent(ch);
		}

		void ConsoleScreen::TextEditingEvent(const std::string& ch, int start, int len) {
			SPADES_MARK_FUNCTION();

			if (ShouldInterceptInput())
				ui->TextEditingEvent(ch, start, len);
			else
				subview->TextEditingEvent(ch, start, len);
		}

		bool ConsoleScreen::AcceptsTextInput() {
			SPADES_MARK_FUNCTION();

			if (ShouldInterceptInput())
				return ui->AcceptsTextInput();
			else
				return subview->AcceptsTextInput();
		}

		AABB2 ConsoleScreen::GetTextInputRect() {
			SPADES_MARK_FUNCTION();

			if (ShouldInterceptInput())
				return ui->GetTextInputRect();
			else
				return subview->GetTextInputRect();
		}

		void ConsoleScreen::WheelEvent(float x, float y) {
			SPADES_MARK_FUNCTION();

			if (ShouldInterceptInput())
				ui->WheelEvent(x, y);
			else
				subview->WheelEvent(x, y);
		}

		bool ConsoleScreen::NeedsAbsoluteMouseCoordinate() {
			SPADES_MARK_FUNCTION();

			return ShouldInterceptInput() ? true : subview->NeedsAbsoluteMouseCoordinate();
		}

		void ConsoleScreen::RunFrame(float dt) {
			SPADES_MARK_FUNCTION();

			// Read the global log buffer dedicated to `ConsoleScreen`
			GetBufferedLogLines(stmp::make_fn<void(std::string)>([this](std::string entry) {
				for (const auto& line : SplitIntoLines(entry))
					AddLine(line);
			}));

			subview->RunFrame(dt);

			ui->RunFrame(dt);
		}

		void ConsoleScreen::RunFrameLate(float dt) {
			SPADES_MARK_FUNCTION();
			subview->RunFrameLate(dt);
		}

		void ConsoleScreen::Closing() {
			SPADES_MARK_FUNCTION();
			subview->Closing();
		}

		bool ConsoleScreen::WantsToBeClosed() {
			SPADES_MARK_FUNCTION();
			return subview->WantsToBeClosed();
		}

		void ConsoleScreen::DumpAllCommands() {
			SPADES_MARK_FUNCTION();
			auto it = AutocompleteCommandName("");
			while (it->MoveNext()) {
				const auto& cmd = it->GetCurrent();
				SPLog("%s %s", cmd.name.c_str(), cmd.description.c_str());
			}
		}

		namespace {
			constexpr const char* CMD_HELP = "help";
			constexpr const char* CMD_CLEARGFXCACHE = "cleargfxcache";
			constexpr const char* CMD_CLEARSFXCACHE = "clearsfxcache";

			std::map<std::string, std::string> const g_commands{
			  {CMD_HELP, ": Display all available commands"},
			  {CMD_CLEARGFXCACHE, ": Clear the GFX (models and images) cache, forcing reload"},
			  {CMD_CLEARSFXCACHE, ": Clear the SFX cache, forcing reload"},
			};
		} // namespace

		bool ConsoleScreen::ExecCommand(const Handle<ConsoleCommand>& command) {
			SPADES_MARK_FUNCTION();
			if (command->GetName() == CMD_HELP) {
				if (command->GetNumArguments() != 0) {
					SPLog("Usage: %s (no arguments)", CMD_HELP);
					return true;
				}
				DumpAllCommands();
				return true;
			} else if (command->GetName() == CMD_CLEARGFXCACHE) {
				if (command->GetNumArguments() != 0) {
					SPLog("Usage: %s (no arguments)", CMD_CLEARGFXCACHE);
					return true;
				}
				renderer->ClearCache();
				return true;
			} else if (command->GetName() == CMD_CLEARSFXCACHE) {
				if (command->GetNumArguments() != 0) {
					SPLog("Usage: %s (no arguments)", CMD_CLEARSFXCACHE);
					return true;
				}
				audioDevice->ClearCache();
				return true;
			}
			return ConfigConsoleResponder::ExecCommand(command) || subview->ExecCommand(command);
		}

		Handle<ConsoleCommandCandidateIterator>
		ConsoleScreen::AutocompleteCommandName(const std::string& name) {
			SPADES_MARK_FUNCTION();
			return MakeCandidates(g_commands, name) +
				   ConfigConsoleResponder::AutocompleteCommandName(name) +
				   subview->AutocompleteCommandName(name);
		}

		bool ConsoleScreen::ShouldInterceptInput() {
			SPADES_MARK_FUNCTION();
			s_consoleOpen = ui->ShouldInterceptInput();
			return s_consoleOpen;
		}

		bool ConsoleScreen::IsConsoleOpen() { return s_consoleOpen; }

		void ConsoleScreen::ToggleConsole() {
			SPADES_MARK_FUNCTION();
			ui->ToggleConsole();
		}

		void ConsoleScreen::AddLine(const std::string& line) {
			SPADES_MARK_FUNCTION();
			ui->AddLine(line);
		}
	} // namespace gui
} // namespace spades
