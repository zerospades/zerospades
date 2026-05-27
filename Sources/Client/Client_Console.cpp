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
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Client.h"
#include "NetClient.h"

#include <Gui/ConsoleCommand.h>

namespace spades {
	namespace client {
		namespace {
			constexpr const char* CMD_SAVEMAP = "savemap";
			constexpr const char* CMD_SETBLOCKCOLOR = "setblockcolor";

			std::map<std::string, std::string> const g_clientCommands{
			  {CMD_SAVEMAP, ": Save the current state of the map to the disk"},
			  {CMD_SETBLOCKCOLOR, ": Set the block color (all values 0-255)"},
			};
		} // namespace

		bool Client::ExecCommand(const Handle<gui::ConsoleCommand>& cmd) {
			if (cmd->GetName() == CMD_SAVEMAP) {
				if (cmd->GetNumArguments() != 0) {
					SPLog("Usage: %s (no arguments)", CMD_SAVEMAP);
					return true;
				}
				TakeMapShot();
				return true;
			} else if (cmd->GetName() == CMD_SETBLOCKCOLOR) {
				if (cmd->GetNumArguments() == 3) {
					int r = Clamp(std::stoi(cmd->GetArgument(0)), 0, 255);
					int g = Clamp(std::stoi(cmd->GetArgument(1)), 0, 255);
					int b = Clamp(std::stoi(cmd->GetArgument(2)), 0, 255);
					SetBlockColor(MakeIntVector3(r, g, b));
				} else {
					SPLog("Invalid number of arguments (Maybe you meant something "
					      "like 'varname \"value1 value2 value3\"'?)");
					return true;
				}
				return true;
			} else {
				return false;
			}
		}

		Handle<gui::ConsoleCommandCandidateIterator>
		Client::AutocompleteCommandName(const std::string& name) {
			return gui::MakeCandidates(g_clientCommands, name);
		}
	} // namespace client
} // namespace spades
