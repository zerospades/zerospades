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

#include <Client/GameConstants.h> // TC_CAPTURE_RATE (=0.05F)
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
						// Oracle fidelity (CR-01): ExistingPlayer does NOT set orientation —
						// only a later WorldUpdate (SetOrientation, :517) does. Until then the
						// player carries the Player-ctor team default (Player.cpp:56), NOT a
						// zero from savedFront. A subsequent WorldUpdate overwrites this.
						p.orientation = MakeVector3((s.team == 1) ? -1.0F : 1.0F, 0.0F, 0.0F);
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
						// Same oracle fidelity as ExistingPlayer (CR-01): the freshly
						// constructed Player carries the team-default orientation
						// (Player.cpp:56) until a WorldUpdate sets it.
						p.orientation = MakeVector3((s.team == 1) ? -1.0F : 1.0F, 0.0F, 0.0F);
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
					case PacketTypeKillAction: {
						// NetClient.cpp:880-910. The oracle's Player::KilledBy sets the victim
						// alive=false, health=0 (NetClient.cpp:907 → Player.cpp KilledBy).
						auto s = DecodeKillAction(r);
						int victimId = s.victimId;
						int killerId = s.killerId;
						int kt = s.killType;
						// NetClient.cpp:888-897 — killType 0..6 → KillType. We only need the
						// self-kill remap below; the exact KillType value is not stored, but the
						// kt switch is reproduced so out-of-range input SPRaises like the oracle.
						switch (kt) {
							case 0: // KillTypeWeapon
							case 1: // KillTypeHeadshot
							case 2: // KillTypeMelee
							case 3: // KillTypeGrenade
							case 4: // KillTypeFall
							case 5: // KillTypeTeamChange
							case 6: // KillTypeClassChange
								break;
							default: SPRaise("Invalid kill type %d", kt);
						}
						// NetClient.cpp:898-903 — Fall(4)/TeamChange(5)/ClassChange(6) are
						// self-kills: killerId is remapped to victimId.
						if (kt == 4 || kt == 5 || kt == 6)
							killerId = victimId;
						// WR-01: the oracle resolves the victim via NetClient::GetPlayer,
						// which SPRaises for an out-of-range id or a null player slot
						// (NetClient.cpp). Mirror that here instead of letting
						// std::map::operator[] default-construct a phantom victim — a
						// Rust port reading "mirrors NetClient" must reproduce the throw.
						// NetClient.cpp:905-907 — victim.KilledBy → alive=false (health=0 at
						// ToJson per OQ-2). We model only the alive flag here.
						auto victimIt = snap.players.find(victimId);
						if (victimIt == snap.players.end())
							SPRaise("KillAction for unknown player id %d", victimId);
						victimIt->second.alive = false;
						// NetClient.cpp:908-909 — killer persistent score++ ONLY when the kill
						// is not a self-kill (Pitfall 8: prevents over-counting Fall/etc).
						// The killer is also resolved via GetPlayer in the oracle (:908), so a
						// non-self killer id that is unknown SPRaises here too. (Self-kills
						// short-circuit: killerId == victimId, already validated above.)
						if (killerId != victimId) {
							if (snap.players.find(killerId) == snap.players.end())
								SPRaise("KillAction for unknown killer id %d", killerId);
							snap.persistentScore[killerId]++;
						}
					} break;
					case PacketTypePlayerLeft: {
						// NetClient.cpp:962-971 — GetPlayerPersistent(pId).score = 0;
						// savedPlayerTeam[pId] = -1; SetPlayer(pId, NULL) removes the slot.
						// CR-02 / Pitfall 9: erase the player so a subsequent WorldUpdate for
						// the same index does NOT resurrect it (the WorldUpdate branch above
						// gates on players.find(index) != end(), so erasure is sufficient).
						int pId = DecodePlayerLeft(r).playerId;
						snap.persistentScore[pId] = 0; // :967
						snap.players.erase(pId);       // :970 SetPlayer(pId, NULL)
					} break;
					case PacketTypeIntelPickup: {
						// NetClient.cpp:1073-1091 — team(of p).hasIntel = true; carrierId = pId.
						// Divergence: the oracle SPRaises in non-CTF mode (:1080-1081); the fold
						// tracks CTF state unconditionally because tests feed only correct-mode
						// sequences for the value_lookup path (D-01 logical accumulator).
						int pId = DecodeIntelPickup(r).playerId;
						int teamId = snap.players.count(pId) ? snap.players[pId].teamId : 0;
						if (teamId >= 0 && teamId < 2) {
							snap.ctfHasIntel[teamId] = true;  // :1088
							snap.ctfCarrierId[teamId] = pId;  // :1089
						}
					} break;
					case PacketTypeIntelCapture: {
						// NetClient.cpp:1041-1072 — team(of p).score++; team.hasIntel=false;
						// GetPlayerPersistent(pId).score += 10; if winning ResetIntelHoldingStatus
						// (zeroes BOTH team scores and clears BOTH hasIntel — CTFGameMode.cpp:48-54).
						auto s = DecodeIntelCapture(r);
						int pId = s.playerId;
						int teamId = snap.players.count(pId) ? snap.players[pId].teamId : 0;
						if (teamId >= 0 && teamId < 2) {
							snap.ctfScore[teamId]++;          // :1061
							snap.ctfHasIntel[teamId] = false; // :1062
						}
						snap.persistentScore[pId] += 10; // :1065
						if (s.winning != 0) {                 // :1067-1071 winning capture
							// ResetIntelHoldingStatus(true): zero both scores + clear both intel.
							snap.ctfScore[0] = 0;
							snap.ctfScore[1] = 0;
							snap.ctfHasIntel[0] = false;
							snap.ctfHasIntel[1] = false;
						}
					} break;
					case PacketTypeIntelDrop: {
						// NetClient.cpp:1092-1110 — team(teamId).hasIntel=false;
						// team(1-teamId).flagPos = s.position.
						auto s = DecodeIntelDrop(r);
						int pId = s.playerId;
						int teamId = snap.players.count(pId) ? snap.players[pId].teamId : 0;
						if (teamId >= 0 && teamId < 2) {
							snap.ctfHasIntel[teamId] = false;       // :1107
							snap.ctfFlagPos[1 - teamId] = s.position; // :1108
						}
					} break;
					case PacketTypeTerritoryCapture: {
						// NetClient.cpp:972-1007 — territory.ownerTeamId = state;
						// progressBasePos/Rate/StartTime = 0; capturingTeamId = -1.
						// Divergence: oracle SPRaises in non-TC mode / on bad territoryId; the
						// fold grows the territory vector on demand (tests feed valid ids).
						auto s = DecodeTerritoryCapture(r);
						int tid = s.territoryId;
						if ((size_t)tid >= snap.territories.size())
							snap.territories.resize(tid + 1);
						WorldSnapshot::TerritoryState& t = snap.territories[tid];
						t.ownerTeamId = s.state;     // :999
						t.progressBasePos = 0.0F;    // :1000
						t.progressRate = 0.0F;       // :1001
						t.progressStartTime = 0.0F;  // :1002
						t.capturingTeamId = -1;      // :1003
					} break;
					case PacketTypeProgressBar: {
						// NetClient.cpp:1008-1040 — progressBasePos = progress;
						// progressRate = (float)rate * TC_CAPTURE_RATE; capturingTeamId =
						// capturingTeam; progressStartTime = GetWorld()->GetTime().
						// Divergence: the fold has no World clock — progressStartTime is set to
						// 0 (logical) so GetProgress() in a test is read against a pinned World
						// time supplied by the test, not the fold. rate is SIGNED (int8_t,
						// ProtocolCodec.h:562) so the sign math is carried by rate's sign.
						auto s = DecodeProgressBar(r);
						int tid = s.territoryId;
						if ((size_t)tid >= snap.territories.size())
							snap.territories.resize(tid + 1);
						WorldSnapshot::TerritoryState& t = snap.territories[tid];
						t.progressBasePos = s.progress;                       // :1036
						t.progressRate = (float)s.rate * TC_CAPTURE_RATE;     // :1037
						t.progressStartTime = 0.0F;                           // :1038 (no clock)
						t.capturingTeamId = s.capturingTeam;                  // :1039
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
				// A1 fold constant — protocol carries no velocity (Pitfall 3 / Q5).
				pj["velocity"] = {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}};
				pj["orientation"] =
				  {{"x", p.orientation.x}, {"y", p.orientation.y}, {"z", p.orientation.z}};
				// OQ-2 (Phase 7): dead players emit health=0, revising the Phase-4 constant 100.
				// The oracle's Player::KilledBy sets health=0 (NetClient.cpp:907 KillAction).
				pj["health"] = p.alive ? 100 : 0;
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
