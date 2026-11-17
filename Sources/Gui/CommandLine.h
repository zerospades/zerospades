/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <Core/ServerAddress.h>

namespace spades {
	/**
	 * What the program was asked to do on its command line, parsed once by
	 * ParseCommandLine and read from here by whoever acts on it.
	 */
	struct CommandLine {
		// --- What to start: one of these at most (the main screen otherwise)
		/** A server to connect to (`aos://...`), with its protocol version. */
		std::string serverAddress;
		ProtocolVersion protocolVersion = ProtocolVersion::v075;
		/** A demo to play back (--replay). */
		std::string replayPath;
		/** A demo to play back following a player, with no menus
		 *  (--replay-demo, with --demo and --player). */
		bool replayFollowing = false;
		std::string followDemo;   // empty for the latest in Demos/
		std::string followPlayer; // empty for the first player; else an id or a name
		/** A file to open in its editor (--open, or the file on its own). */
		std::string openDocument;

		// --- How to start it
		/** A mod to try on its own (--try-mod). */
		std::string tryMod;
		/** Open the main screen on its Mods tab (--open-mods). */
		bool openModsTab = false;
		/** Config variables set for this run only (--override-cvar). */
		std::vector<std::pair<std::string, std::string>> cvarOverrides;
		/** Seconds after start to take a screenshot and quit; negative for
		 *  never (--screenshot-quit). */
		float screenshotQuitAfter = -1.0F;
		/** Reference images for the model editor --open opens, each across
		 *  an axis: 0 = x, 1 = y, 2 = z (--editor-reference). */
		std::vector<std::pair<int, std::string>> editorReferences;

		// --- Something else than the game, done and over with
		bool printHelp = false;
		bool printVersion = false;
		bool registerFileTypes = false;
		bool unregisterFileTypes = false;
		/** The arguments of --kv6-tools, which takes all that follow it. */
		bool kv6Tools = false;
		std::vector<std::string> kv6ToolsArguments;

		/** Whether the options for developers and tests are allowed. */
		bool debugTools = false;
	};

	/** What ParseCommandLine found. */
	enum class CommandLineResult {
		Run,  // parsed; go on
		Error // a usage error, already reported on stderr: exit with code 2
	};

	/**
	 * Parses `argv` into `out`.
	 *
	 * A usage error -- an option missing its value, a value that is not one,
	 * an option for developers without --debug-tools, options that contradict
	 * each other -- is reported on stderr and makes the result Error. An option
	 * that is not known is warned about and skipped, along with the value it
	 * may have brought, as a system may add options of its own when it starts
	 * a program. A bare `--` ends the options, as it does everywhere.
	 */
	CommandLineResult ParseCommandLine(int argc, char** argv, CommandLine& out);

	/** Prints the options a player may use, and with `debugTools` the ones
	 *  for developers and tests as well. `program` is how the program was
	 *  started. */
	void PrintCommandLineHelp(const char* program, bool debugTools);

	/**
	 * A local path from what a desktop hands over: a `file://` URL as a file
	 * manager passes one (percent-encoded), or a path, returned unchanged.
	 */
	std::string LocalPathFromArgument(const std::string& arg);
} // namespace spades
