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

#include "ServerListMerge.h"

#include <algorithm>
#include <climits>
#include <unordered_map>

namespace spades {
	namespace gui {
		std::string ServerListString(const Json::Value& obj, const char* key) {
			if (!obj.isObject())
				return {};
			const Json::Value& v = obj[key];
			return v.isString() ? v.asString() : std::string();
		}

		int ServerListInt(const Json::Value& obj, const char* key) {
			if (!obj.isObject())
				return 0;
			const Json::Value& v = obj[key];
			if (!v.isNumeric())
				return 0;
			const double d = v.asDouble();
			if (!(d >= INT_MIN && d <= INT_MAX)) // also rejects NaN
				return 0;
			return static_cast<int>(d);
		}

		int ServerListPlayers(const Json::Value& obj, const char* key) {
			return std::min(std::max(ServerListInt(obj, key), 0), kMaxServerListPlayers);
		}

		std::string ServerListIdentifier(const Json::Value& obj) {
			const std::string s = ServerListString(obj, "identifier");
			const char* const space = " \t\r\n";
			const std::size_t first = s.find_first_not_of(space);
			if (first == std::string::npos)
				return {};
			return s.substr(first, s.find_last_not_of(space) - first + 1);
		}

		bool ServerListNestingWithin(const std::string& json, int maxDepth) {
			int depth = 0;
			bool inString = false;
			bool escaped = false;
			for (char c : json) {
				if (inString) {
					if (escaped)
						escaped = false;
					else if (c == '\\')
						escaped = true;
					else if (c == '"')
						inString = false;
					continue;
				}
				if (c == '"') {
					inString = true;
				} else if (c == '[' || c == '{') {
					if (++depth > maxDepth)
						return false;
				} else if (c == ']' || c == '}') {
					--depth;
				}
			}
			return true;
		}

		Json::Value MergeServerLists(const std::vector<Json::Value>& lists) {
			struct Merged {
				Json::Value entry; // fields from the first list that has the server
				int playersCurrent;
				int playersMax;
			};

			std::vector<Merged> merged;
			std::unordered_map<std::string, std::size_t> byIdentifier;

			for (const Json::Value& list : lists) {
				// The bundled jsoncpp also calls null an array.
				if (list.type() != Json::arrayValue)
					continue;
				for (const Json::Value& entry : list) {
					const std::string identifier = ServerListIdentifier(entry);
					if (identifier.empty())
						continue;

					const int playersCurrent = ServerListPlayers(entry, "players_current");
					const int playersMax = ServerListPlayers(entry, "players_max");

					auto it = byIdentifier.find(identifier);
					if (it == byIdentifier.end()) {
						byIdentifier.emplace(identifier, merged.size());
						merged.push_back({entry, playersCurrent, playersMax});
						merged.back().entry["identifier"] = identifier;
						continue;
					}

					Merged& m = merged[it->second];
					m.playersCurrent = std::max(m.playersCurrent, playersCurrent);
					// The highest limit wins because legacy server lists (Build and
					// Shoot's, and its mirrors) show at most 32, even when the server
					// supports more; another list may report the real limit, such as
					// LSd's server list, by "Not a Burner", the author of LSd.
					m.playersMax = std::max(m.playersMax, playersMax);
				}
			}

			Json::Value result(Json::arrayValue);
			for (Merged& m : merged) {
				m.entry["players_current"] = m.playersCurrent;
				m.entry["players_max"] = m.playersMax;
				result.append(m.entry);
			}
			return result;
		}
	} // namespace gui
} // namespace spades
