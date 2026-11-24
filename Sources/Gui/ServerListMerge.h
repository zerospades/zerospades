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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <string>
#include <vector>

#include <json/json.h>

namespace spades {
	namespace gui {
		/**
		 * `obj[key]` if it is a string, else an empty string. Server list entries
		 * come from the network, and jsoncpp throws on a type mismatch.
		 */
		std::string ServerListString(const Json::Value& obj, const char* key);

		/** `obj[key]` if it is a number that fits an `int`, else 0. */
		int ServerListInt(const Json::Value& obj, const char* key);

		/**
		 * The most players a server can report. Player counts and limits always fit
		 * in a byte: the server and the master send them as `uint8_t`.
		 */
		constexpr int kMaxServerListPlayers = 255;

		/** `obj[key]` as a player count or limit, clamped to 0..`kMaxServerListPlayers`. */
		int ServerListPlayers(const Json::Value& obj, const char* key);

		/** `obj["identifier"]` without surrounding whitespace; empty if absent. */
		std::string ServerListIdentifier(const Json::Value& obj);

		/**
		 * The deepest nesting of arrays and objects a server list may use. A real
		 * list nests 2 deep; the bundled jsoncpp parses by recursion, so a deeply
		 * nested response could overflow the stack of the thread parsing it.
		 */
		constexpr int kMaxServerListNesting = 64;

		/**
		 * Whether `json` nests arrays and objects at most `maxDepth` deep. Brackets
		 * inside strings do not count. Malformed input is left for the parser to
		 * reject; only the depth is checked here.
		 */
		bool ServerListNestingWithin(const std::string& json, int maxDepth);

		/**
		 * Merges server lists (each a JSON array of entries in the
		 * `serverlist.json` format) into one array, one entry per `identifier`
		 * (compared without surrounding whitespace).
		 *
		 * When several lists report the same server, `players_current` and
		 * `players_max` take the highest value reported, each clamped to
		 * 0..`kMaxServerListPlayers`: some masters cap capacity at 32 although
		 * servers go up to 255 players, and counts drift between lists. Every
		 * other field comes from the first list that reports the server, so list
		 * order is trust order: a later list cannot rename a server, or change the
		 * game version used to connect to it, by claiming a newer report.
		 *
		 * Entries without a string `identifier` are dropped, and so is anything in
		 * `lists` that is not an array. The result keeps the order in which
		 * servers first appear.
		 */
		Json::Value MergeServerLists(const std::vector<Json::Value>& lists);
	} // namespace gui
} // namespace spades
