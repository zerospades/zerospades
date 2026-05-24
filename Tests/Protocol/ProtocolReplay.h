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

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Client/ProtocolCodec.h>
#include <Core/Math.h>

namespace spades {
	namespace tests {

		// Logical per-player state accumulated by the protocol fold. This is NOT a
		// spades::client::Player — it carries only the fields the wire protocol
		// determines (CONTEXT D-01). velocity/health are NOT stored: they are emitted
		// as fold constants at ToJson() time (A1: velocity {0,0,0}, health 100), since
		// no golden packet sequence carries them (RESEARCH Pitfall 3 / Q5).
		struct PlayerState {
			uint8_t id = 0;
			bool alive = false;
			spades::Vector3 position{};    // savedPlayerPos / CreatePlayer pos.z-2.4 / WorldUpdate
			spades::Vector3 orientation{}; // WorldUpdate front (non-local alive players)
			uint8_t teamId = 0;
			std::string weaponType; // "rifle" | "smg" | "shotgun"
			std::string tool;       // "spade" | "block" | "weapon" | "grenade"
		};

		// Logical WorldSnapshot: the protocol-driven world state produced by folding a
		// packet sequence through the frozen Phase-3 codec. ToJson() emits an object
		// conformant to fixtures/fixture_schema.json world_snapshot.expected.
		//
		// players is keyed by id (std::map → deterministic id-ascending iteration,
		// RESEARCH Pitfall 5). extensions records ALL server-advertised ids→version
		// (A3 — the portable wire fact, NOT NetClient's implementedExtensions filter).
		struct WorldSnapshot {
			int tick = 0;
			std::map<uint8_t, PlayerState> players;
			std::string gameMode;          // "" when unset; "ctf" | "tc"
			int localPlayerIndex = -1;
			std::map<uint8_t, uint8_t> extensions;

			// --- Game-mode + persistent state (Phase 7, CR-02 + MODE-01/02) ---
			// These members are read DIRECTLY by value_lookup tests; they are NOT
			// serialized into ToJson() (which stays schema-conformant: only
			// tick/players[]/game_mode.mode). This mirrors how localPlayerIndex and
			// extensions above are fold members consumed by tests but not all emitted.

			// Persistent per-player score, keyed by player id. Mirrors the oracle's
			// World::GetPlayerPersistent(id).score (NetClient.cpp:909 killer++,
			// :967 leave→0, :1065 intel-capture +10).
			std::map<uint8_t, int> persistentScore;

			// CTF team state, indexed [0..1]. Field-name parallel to CTFGameMode::Team
			// (CTFGameMode.h:33-39): score / hasIntel / carrierId / flagPos.
			unsigned int ctfScore[2] = {0, 0};
			bool ctfHasIntel[2] = {false, false};
			unsigned int ctfCarrierId[2] = {0, 0};
			spades::Vector3 ctfFlagPos[2]{};

			// TC territory state. Field-name parallel to TCGameMode::Territory
			// (TCGameMode.h:35-54): ownerTeamId / capturingTeamId / progressBasePos /
			// progressRate / progressStartTime. GetProgress() = progressBasePos +
			// progressRate * (time - progressStartTime), clamped [0,1] (TCGameMode.cpp).
			struct TerritoryState {
				int ownerTeamId = 2; // 2 = neutral (TCGameMode.h:40)
				int capturingTeamId = -1;
				float progressBasePos = 0.0F;
				float progressRate = 0.0F;
				float progressStartTime = 0.0F;
			};
			std::vector<TerritoryState> territories;

			nlohmann::json ToJson() const;
		};

		// Decode a hex string ("0900...") into raw packet bytes for NetPacketReader.
		// Rejects odd-length or non-hex input via SPRaise (RESEARCH Pitfall 6) — never
		// returns garbage bytes.
		std::vector<char> HexDecode(const std::string& hex);

		// Fold a sequence of raw packet byte vectors into a WorldSnapshot, mirroring
		// NetClient::HandleGamePacket semantics (line-cited in the .cpp). protocolVersion
		// 3 = 0.75, 4 = 0.76 (passed to DecodeWorldUpdate, D-08).
		WorldSnapshot ReplaySnapshot(const std::vector<std::vector<char>>& packets,
		                             int protocolVersion);

		// Map codec weapon int → schema weapon_type enum string. Out-of-range throws via
		// SPRaise, mirroring NetClient :614/:708 switch defaults.
		std::string WeaponName(uint8_t weapon);

		// Map codec tool int → schema tool enum string. Out-of-range throws via SPRaise,
		// mirroring NetClient :630 switch default.
		std::string ToolName(uint8_t tool);

	} // namespace tests
} // namespace spades
