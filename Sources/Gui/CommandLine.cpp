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

#include "CommandLine.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

#include <Core/Strings.h>
#include <Gui/DocumentTypes.h>

namespace spades {
	namespace {
		/** What an option takes from the arguments after it and does with them:
		 *  an empty string where it was fine, else what was wrong. */
		using Apply = std::function<std::string(CommandLine&, const std::vector<std::string>&)>;

		/**
		 * An option, in the one table the parser, the check that options for
		 * developers come with --debug-tools, and the help are all made from.
		 */
		struct Option {
			const char* name;
			const char* shortName; // or null
			const char* values;    // as the help names them, or null for none
			int valueCount;
			bool forDevelopers; // refused without --debug-tools
			const char* help;   // lines parted by '\n'
			Apply apply;
		};

		bool IsName(const std::string& arg, const char* name) {
			return name && EqualsIgnoringCase(arg, name);
		}

		bool ParseSeconds(const std::string& text, float& out) {
			const char* begin = text.c_str();
			char* end = nullptr;
			const float value = std::strtof(begin, &end);
			if (end == begin || *end != '\0' || !std::isfinite(value) || value < 0.0F)
				return false;
			out = value;
			return true;
		}

		const std::vector<Option>& Options() {
			static const std::vector<Option> options = {
			  {"--replay", "-r", "FILE", 1, false, "play back a demo recording",
			   [](CommandLine& c, const std::vector<std::string>& v) {
				   c.replayPath = v[0];
				   return std::string();
			   }},
			  {"--open", nullptr, "FILE", 1, false,
			   "open a file in its editor, skipping the menus. A file\n"
			   "given on its own does the same, which is what a file\n"
			   "manager passes when it opens one with this program.",
			   [](CommandLine& c, const std::vector<std::string>& v) {
				   const std::string path = LocalPathFromArgument(v[0]);
				   // An editor writes what it opens: handing it a file of
				   // another type is how that file gets something else
				   // written over it.
				   if (!gui::IsEditableDocument(path))
					   return "not a file this program can open: " + path;
				   c.openDocument = path;
				   return std::string();
			   }},
			  {"--try-mod", nullptr, "PATH", 1, false,
			   "try a mod (folder or .pak) on top of the base game; it\n"
			   "bypasses the startup setup and the enabled-mod set, and\n"
			   "hides the Mods tab, so only this mod applies. A bare\n"
			   "name resolves under the user Mods/ folder.",
			   [](CommandLine& c, const std::vector<std::string>& v) {
				   c.tryMod = v[0];
				   return std::string();
			   }},
			  {"--open-mods", nullptr, nullptr, 0, false,
			   "open the main menu on its Mods tab, skipping the startup\n"
			   "setup (what the program restarts itself with after the\n"
			   "enabled mods changed)",
			   [](CommandLine& c, const std::vector<std::string>&) {
				   c.openModsTab = true;
				   return std::string();
			   }},
			  {"--register-file-types", nullptr, nullptr, 0, false,
			   "offer this program for the files its editors open, so\n"
			   "a file manager can open one with it. Windows only, for\n"
			   "the current user; elsewhere this is done when the\n"
			   "program is installed.",
			   [](CommandLine& c, const std::vector<std::string>&) {
				   c.registerFileTypes = true;
				   return std::string();
			   }},
			  {"--unregister-file-types", nullptr, nullptr, 0, false, "undo --register-file-types",
			   [](CommandLine& c, const std::vector<std::string>&) {
				   c.unregisterFileTypes = true;
				   return std::string();
			   }},
			  {"--help", "-h", nullptr, 0, false,
			   "show this help; with --debug-tools, the options for\n"
			   "developers and tests",
			   [](CommandLine& c, const std::vector<std::string>&) {
				   c.printHelp = true;
				   return std::string();
			   }},
			  {"--version", "-v", nullptr, 0, false, "show the version",
			   [](CommandLine& c, const std::vector<std::string>&) {
				   c.printVersion = true;
				   return std::string();
			   }},
			  {"--debug-tools", nullptr, nullptr, 0, false,
			   "allow the options for developers and tests",
			   [](CommandLine& c, const std::vector<std::string>&) {
				   c.debugTools = true;
				   return std::string();
			   }},

			  {"--override-cvar", nullptr, "NAME=VALUE", 1, true,
			   "set a config variable for this run only; the config on\n"
			   "disk is left as it was. May be given more than once.",
			   [](CommandLine& c, const std::vector<std::string>& v) {
				   const std::size_t eq = v[0].find('=');
				   if (eq == std::string::npos || eq == 0)
					   return "--override-cvar takes NAME=VALUE, not " + v[0];
				   c.cvarOverrides.emplace_back(v[0].substr(0, eq), v[0].substr(eq + 1));
				   return std::string();
			   }},
			  {"--replay-demo", nullptr, nullptr, 0, true,
			   "play a demo following a player, skipping every menu;\n"
			   "--demo and --player say which",
			   [](CommandLine& c, const std::vector<std::string>&) {
				   c.replayFollowing = true;
				   return std::string();
			   }},
			  {"--demo", nullptr, "FILE", 1, true,
			   "the demo --replay-demo plays (default: the latest in\n"
			   "Demos/); a bare name resolves under Demos/",
			   [](CommandLine& c, const std::vector<std::string>& v) {
				   c.followDemo = v[0];
				   return std::string();
			   }},
			  {"--player", nullptr, "ID|NAME", 1, true,
			   "the player --replay-demo follows (default: the first)",
			   [](CommandLine& c, const std::vector<std::string>& v) {
				   c.followPlayer = v[0];
				   return std::string();
			   }},
			  {"--editor-reference", nullptr, "X|Y|Z IMAGE", 2, true,
			   "show IMAGE as a reference image across that axis in\n"
			   "the model editor that --open opens",
			   [](CommandLine& c, const std::vector<std::string>& v) {
				   const int axis = EqualsIgnoringCase(v[0], "x")   ? 0
				                    : EqualsIgnoringCase(v[0], "y") ? 1
				                    : EqualsIgnoringCase(v[0], "z") ? 2
				                                                    : -1;
				   if (axis < 0)
					   return "--editor-reference: the axis is X, Y or Z, not " + v[0];
				   c.editorReferences.emplace_back(axis, LocalPathFromArgument(v[1]));
				   return std::string();
			   }},
			  {"--screenshot-quit", nullptr, "SECONDS", 1, true,
			   "take a screenshot that many seconds after starting,\n"
			   "then quit: to check what is drawn with no one at the\n"
			   "keyboard",
			   [](CommandLine& c, const std::vector<std::string>& v) {
				   if (!ParseSeconds(v[0], c.screenshotQuitAfter))
					   return "--screenshot-quit takes a number of seconds, not " + v[0];
				   return std::string();
			   }},
			  // Takes every argument after it as its own: see ParseCommandLine.
			  {"--kv6-tools", nullptr, "COMMAND [ARGS...]", 0, true,
			   "read and edit a KV6 model without opening the game\n"
			   "(its size, pivot, bounds, voxels). Everything after it\n"
			   "is its own, so only --debug-tools may come before it;\n"
			   "--debug-tools --kv6-tools help lists its commands.",
			   nullptr},
			};
			return options;
		}

		const Option* FindOption(const std::string& arg) {
			for (const Option& option : Options()) {
				if (IsName(arg, option.name) || IsName(arg, option.shortName))
					return &option;
			}
			return nullptr;
		}

		// `aos://...`, as a server address is written; the scheme in any case.
		bool IsServerAddress(const std::string& arg) {
			static const std::string scheme = "aos://";
			return arg.size() > scheme.size() && EqualsIgnoringCase(arg.substr(0, scheme.size()), scheme);
		}

		// The protocol version a server address is spoken to with: `v=0.75`,
		// `v=0.76`, or the number alone.
		bool ParseProtocolVersion(const std::string& arg, ProtocolVersion& out) {
			std::string text = arg;
			if (text.size() > 2 && EqualsIgnoringCase(text.substr(0, 2), "v="))
				text = text.substr(2);
			if (text == "0.75") {
				out = ProtocolVersion::v075;
				return true;
			}
			if (text == "0.76") {
				out = ProtocolVersion::v076;
				return true;
			}
			return false;
		}

		void Error(const std::string& message) {
			std::fprintf(stderr, "%s\n(--help lists the options)\n", message.c_str());
		}
	} // namespace

	std::string LocalPathFromArgument(const std::string& arg) {
		static const std::string scheme = "file://";
		if (arg.compare(0, scheme.size(), scheme) != 0)
			return arg;

		// Everything between the scheme and the path is the host, which is empty
		// or "localhost" for a file this machine can open; either way the path
		// starts at the slash that follows it.
		const std::size_t start = arg.find('/', scheme.size());
		if (start == std::string::npos)
			return arg;

		std::string path;
		path.reserve(arg.size() - start);
		for (std::size_t i = start; i < arg.size(); i++) {
			if (arg[i] == '%' && i + 2 < arg.size() && std::isxdigit((unsigned char)arg[i + 1]) &&
			    std::isxdigit((unsigned char)arg[i + 2])) {
				path += char(std::strtol(arg.substr(i + 1, 2).c_str(), nullptr, 16));
				i += 2;
			} else {
				path += arg[i];
			}
		}

		// A Windows URL names a drive after the leading slash (`file:///C:/a.kv6`),
		// and the slash is not part of the path it stands for.
		if (path.size() >= 3 && path[0] == '/' && path[2] == ':')
			path.erase(0, 1);

		return path;
	}

	CommandLineResult ParseCommandLine(int argc, char** argv, CommandLine& out) {
		std::vector<std::string> args;
		for (int i = 1; i < argc; i++)
			args.push_back(argv[i] ? argv[i] : "");

		const Option* developerOption = nullptr; // the first given, to name it
		bool versionGiven = false;
		bool anythingBeforeTools = false; // but --debug-tools
		for (std::size_t i = 0; i < args.size(); i++) {
			const std::string& arg = args[i];
			if (arg.empty())
				continue;

			if (arg[0] != '-') {
				// What is not an option: a server address, its protocol
				// version, or a file to open.
				anythingBeforeTools = true;
				if (IsServerAddress(arg)) {
					out.serverAddress = arg;
				} else if (ParseProtocolVersion(arg, out.protocolVersion)) {
					versionGiven = true;
				} else {
					// A bare file is how a file manager hands one over: "open
					// with" passes the file and nothing else, as a path or as a
					// `file://` URL.
					const std::string path = LocalPathFromArgument(arg);
					if (!gui::IsEditableDocument(path)) {
						Error("Not an option, a server address or a file this program can open: " +
						      arg);
						return CommandLineResult::Error;
					}
					out.openDocument = path;
				}
				continue;
			}

			const Option* option = FindOption(arg);
			if (!option) {
				// macOS hands a program launched from the Finder a process
				// serial number of its own; it is not the user's.
				if (arg.compare(0, 5, "-psn_") != 0)
					std::fprintf(stderr, "Ignoring an option this program does not know: %s\n",
					             arg.c_str());
				continue;
			}
			if (option->forDevelopers && !developerOption)
				developerOption = option;

			if (IsName(arg, "--kv6-tools")) {
				// A program of its own: every argument after it is its.
				if (anythingBeforeTools) {
					Error("--kv6-tools takes every argument after it, so only --debug-tools may "
					      "come before it");
					return CommandLineResult::Error;
				}
				out.kv6Tools = true;
				out.kv6ToolsArguments.assign(args.begin() + std::ptrdiff_t(i) + 1, args.end());
				break;
			}
			if (!IsName(arg, "--debug-tools"))
				anythingBeforeTools = true;

			// Its values are the arguments right after it, all of them there.
			if (i + std::size_t(option->valueCount) >= args.size()) {
				Error(std::string(option->name) + " takes " + option->values);
				return CommandLineResult::Error;
			}
			const std::vector<std::string> values(args.begin() + std::ptrdiff_t(i) + 1,
			                                      args.begin() + std::ptrdiff_t(i) + 1 +
			                                        option->valueCount);
			i += std::size_t(option->valueCount);
			const std::string problem = option->apply(out, values);
			if (!problem.empty()) {
				Error(problem);
				return CommandLineResult::Error;
			}
		}

		// --- What was given has to hang together
		// The options for developers and tests are allowed with --debug-tools
		// only, wherever it stands, so none is used by accident.
		if (developerOption && !out.debugTools) {
			if (out.kv6Tools) {
				// One after it would be the tool's own argument, not ours.
				Error("--kv6-tools is a tool for developers and tests: give --debug-tools "
				      "before it");
				return CommandLineResult::Error;
			}
			Error(std::string(developerOption->name) +
			      " is an option for developers and tests: give --debug-tools as well "
			      "(--debug-tools --help lists them all)");
			return CommandLineResult::Error;
		}
		if (out.kv6Tools)
			return CommandLineResult::Run;

		int starts = 0;
		starts += out.serverAddress.empty() ? 0 : 1;
		starts += out.replayPath.empty() ? 0 : 1;
		starts += out.replayFollowing ? 1 : 0;
		starts += out.openDocument.empty() ? 0 : 1;
		if (starts > 1) {
			Error("Give one thing to start at most: a server address, --replay, --replay-demo "
			      "or a file to open");
			return CommandLineResult::Error;
		}
		if (versionGiven && out.serverAddress.empty()) {
			Error("A protocol version is for the server address it comes with, and none was "
			      "given");
			return CommandLineResult::Error;
		}
		if ((!out.followDemo.empty() || !out.followPlayer.empty()) && !out.replayFollowing) {
			Error("--demo and --player say what --replay-demo plays, and it was not given");
			return CommandLineResult::Error;
		}
		if (!out.editorReferences.empty() && out.openDocument.empty()) {
			Error("--editor-reference is for the model editor --open opens, and no file was "
			      "given to open");
			return CommandLineResult::Error;
		}
		if (out.registerFileTypes && out.unregisterFileTypes) {
			Error("--register-file-types and --unregister-file-types undo each other");
			return CommandLineResult::Error;
		}
		return CommandLineResult::Run;
	}

	void PrintCommandLineHelp(const char* program, bool debugTools) {
		// The program's own name, not the path it was started by.
		std::string name = program && *program ? program : "ZeroSpades";
		const std::size_t slash = name.find_last_of("/\\");
		if (slash != std::string::npos)
			name = name.substr(slash + 1);

		const int kColumn = 24; // where an option's help starts
		auto print = [&](const Option& option) {
			std::string head = std::string("  ") + option.name;
			if (option.shortName)
				head += std::string(", ") + option.shortName;
			if (option.values)
				head += std::string(" ") + option.values;
			std::printf("%s", head.c_str());
			int column = int(head.size());
			if (column + 1 > kColumn) {
				std::printf("\n");
				column = 0;
			}
			const std::string help = option.help;
			std::size_t start = 0;
			while (true) {
				const std::size_t end = help.find('\n', start);
				std::printf("%*s%s\n", kColumn - column, "",
				            help.substr(start, end - start).c_str());
				column = 0;
				if (end == std::string::npos)
					break;
				start = end + 1;
			}
		};

		if (debugTools) {
			std::printf("usage: %s --debug-tools [OPTIONS...]\n", name.c_str());
			std::printf("Options for developers and tests, refused without --debug-tools:\n");
		} else {
			std::printf("usage: %s [OPTIONS...] [aos://ADDRESS [v=0.75|v=0.76]] [FILE]\n",
			            name.c_str());
			std::printf("  aos://ADDRESS         connect to that server; v=0.75 (the default)\n");
			std::printf("                        or v=0.76 says its protocol version\n");
			std::printf("  FILE                  a file to open in its editor, as --open\n");
		}
		for (const Option& option : Options()) {
			if (option.forDevelopers == debugTools)
				print(option);
		}
		if (!debugTools) {
			std::printf("\nAuto-recording can be enabled with the cg_demoAutoRecord setting.\n");
			std::printf("Recordings are kept in the Demos/ folder; only the last 10 are retained.\n");
		}
	}
} // namespace spades
