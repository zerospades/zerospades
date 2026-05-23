/*
 Copyright (c) 2013 yvt

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

#include "ProtocolReplay.h"

#include <array>

#include <Core/Exception.h>

using namespace spades::client;

namespace spades {
	namespace tests {

		namespace {
			// Single hex nibble → 0..15, else SPRaise (Pitfall 6).
			int HexNibble(char c) {
				if (c >= '0' && c <= '9')
					return c - '0';
				if (c >= 'a' && c <= 'f')
					return c - 'a' + 10;
				if (c >= 'A' && c <= 'F')
					return c - 'A' + 10;
				SPRaise("Invalid hex character in packet bytes: 0x%02x", (unsigned)(uint8_t)c);
			}
		} // namespace

		std::vector<char> HexDecode(const std::string& hex) {
			// Reject odd-length up front so we never read past the string (Pitfall 6).
			if (hex.size() % 2 != 0)
				SPRaise("Hex packet string has odd length: %zu", hex.size());

			std::vector<char> out;
			out.reserve(hex.size() / 2);
			for (size_t i = 0; i < hex.size(); i += 2) {
				int hi = HexNibble(hex[i]);
				int lo = HexNibble(hex[i + 1]);
				out.push_back(static_cast<char>((hi << 4) | lo));
			}
			return out;
		}

		// weapon 0→rifle, 1→smg, 2→shotgun — mirrors NetClient.cpp:614-619 / :708-713
		// (default: SPRaise "Received invalid weapon").
		std::string WeaponName(uint8_t weapon) {
			switch (weapon) {
				case 0: return "rifle";
				case 1: return "smg";
				case 2: return "shotgun";
				default: SPRaise("Received invalid weapon: %d", (int)weapon);
			}
		}

		// tool 0→spade, 1→block, 2→weapon, 3→grenade — mirrors NetClient.cpp:630-636
		// (default: SPRaise "Received invalid tool type").
		std::string ToolName(uint8_t tool) {
			switch (tool) {
				case 0: return "spade";
				case 1: return "block";
				case 2: return "weapon";
				case 3: return "grenade";
				default: SPRaise("Received invalid tool type: %d", (int)tool);
			}
		}

		// Mirrors spades::client::NetClient::HandleGamePacket (Sources/Client/NetClient.cpp).
		// This is a LOGICAL accumulator (CONTEXT D-01) — NOT NetClient. No World/Player/ENet.
		// Every fold case cites the CURRENT oracle handler line (Pitfall 4).
		WorldSnapshot ReplaySnapshot(const std::vector<std::vector<char>>& packets,
		                             int protocolVersion) {
			WorldSnapshot snap;

			// Mirrors NetClient.cpp:107-112 savedPlayerPos/savedPlayerFront (256 slots).
			std::array<Vector3, 256> savedPos{};
			std::array<Vector3, 256> savedFront{};

			for (const auto& bytes : packets) {
				NetPacketReader r(bytes); // type tag at [0]; pos starts at 1
				switch (r.GetType()) {
					case PacketTypeStateData: {
						// NetClient.cpp:813-879
						auto s = DecodeStateData(r);
						// NetClient.cpp:832 — s.mode == CTFGameMode::m_CTF (0) → CTF, else TC.
						snap.gameMode = (s.mode == 0) ? "ctf" : "tc";
						// NetClient.cpp:830 — SetLocalPlayerIndex(s.playerId).
						snap.localPlayerIndex = s.playerId;
						// Team metadata + CTF scores are wired into the World/GameMode object
						// in the oracle (:824-827, :838-840); they are not part of the
						// world_snapshot.expected player/game_mode shape, so the fold tracks
						// only game_mode + local index here.
					} break;
					case PacketTypeExistingPlayer: {
						// NetClient.cpp:597-647
						auto s = DecodeExistingPlayer(r);
						PlayerState& p = snap.players[s.playerId];
						p.id = s.playerId;
						p.teamId = s.team;
						p.weaponType = WeaponName(s.weapon); // switch :614 (throws on bad)
						p.tool = ToolName(s.tool);           // switch :630 (throws on bad)
						// NetClient.cpp:624 — SetPosition(savedPlayerPos[pId]); position comes
						// from the accumulator's OWN savedPos, NOT this packet (Pitfall 1).
						p.position = savedPos[s.playerId];
						p.orientation = savedFront[s.playerId];
						p.alive = true;
					} break;
					case PacketTypeCreatePlayer: {
						// NetClient.cpp:689-751
						auto s = DecodeCreatePlayer(r);
						PlayerState& p = snap.players[s.playerId];
						p.id = s.playerId;
						p.teamId = s.team;
						p.weaponType = WeaponName(s.weapon); // switch :708 (throws on bad)
						Vector3 pos = s.position;
						pos.z -= 2.4F; // NetClient.cpp:718 — spawn-height adjust (applied in fold)
						p.position = pos;
						// CreatePlayer carries no tool; NetClient relies on the Player-ctor
						// default. Fold convention: "weapon" (A4 — documented, consistent).
						p.tool = "weapon";
						p.alive = true;
					} break;
					case PacketTypeWorldUpdate: {
						// NetClient.cpp:490-527
						auto s = DecodeWorldUpdate(r, protocolVersion);
						for (const auto& e : s.entries) {
							// NetClient.cpp:523-524 — savedPlayerPos/Front written
							// UNCONDITIONALLY for every entry, regardless of player state.
							savedPos[e.index] = e.position;
							savedFront[e.index] = e.front;
							// NetClient.cpp:514-518 — reposition gated on the player existing,
							// being alive, and NOT being the local player (Pitfall 2). We model
							// alive via PlayerState.alive; spectator state is not on the wire.
							auto it = snap.players.find(e.index);
							if (it != snap.players.end() && it->second.alive
							    && e.index != snap.localPlayerIndex) {
								it->second.position = e.position;    // :516 RepositionPlayer
								it->second.orientation = e.front;    // :517 SetOrientation
							}
						}
					} break;
					case PacketTypeExtensionInfo: {
						// NetClient.cpp:448-464 (HandleExtensionPacket). A3: record ALL
						// advertised extensions (the portable wire fact), NOT the oracle's
						// implementedExtensions filter (:454-459).
						auto s = DecodeExtensionInfo(r);
						for (const auto& e : s.extensions)
							snap.extensions[e.id] = e.version;
					} break;
					case PacketTypeMapStart: {
						// NetClient.cpp:945-960 — triggers a map reload in NetClient; no
						// world_snapshot field. Decoded here for coverage / byte advance.
						DecodeMapStart(r);
					} break;
					default: break; // ignore packet types outside the golden fold scope
				}
			}
			return snap;
		}

		// Shape defined by fixtures/fixture_schema.json $defs/PlayerSnapshot +
		// world_snapshot.expected (tick + players[] required; game_mode optional).
		nlohmann::json WorldSnapshot::ToJson() const {
			nlohmann::json j;
			j["tick"] = tick; // 0 for a pure protocol replay (no simulation ticks)

			nlohmann::json playersJson = nlohmann::json::array();
			// std::map iterates id-ascending → deterministic order (Pitfall 5).
			for (const auto& kv : players) {
				const PlayerState& p = kv.second;
				nlohmann::json pj;
				pj["id"] = p.id;
				pj["alive"] = p.alive;
				pj["position"] = {{"x", p.position.x}, {"y", p.position.y}, {"z", p.position.z}};
				// A1 fold constants — protocol carries no velocity/health (Pitfall 3 / Q5).
				pj["velocity"] = {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}};
				pj["orientation"] =
				  {{"x", p.orientation.x}, {"y", p.orientation.y}, {"z", p.orientation.z}};
				pj["health"] = 100;
				pj["tool"] = p.tool;
				pj["weapon_type"] = p.weaponType;
				pj["team_id"] = p.teamId;
				playersJson.push_back(pj);
			}
			j["players"] = playersJson;

			// GameModeSnapshot requires "mode"; emit only when the protocol set it.
			if (!gameMode.empty())
				j["game_mode"] = {{"mode", gameMode}};

			return j;
		}

	} // namespace tests
} // namespace spades
