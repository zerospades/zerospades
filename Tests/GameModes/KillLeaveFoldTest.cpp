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

// CR-02 characterization: closes the Phase-4 fold-gap blocker (STATE.md). Proves
// the extended ProtocolReplay accumulator mirrors the NetClient oracle for
// KillAction (alive=false + self-kill-guarded killer score) and PlayerLeft
// (erase + no-resurrection on a later WorldUpdate). Also proves OQ-2: a dead
// player serializes health=0 (was a constant 100 in Phase 4).
//
// These are FOLD characterization tests, not frozen fixtures: the expected
// values come from the documented oracle semantics, read directly off the
// returned WorldSnapshot — there is no fixture JSON to load.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <Client/ProtocolCodec.h>

#include "../Protocol/ProtocolReplay.h"
#include "SettingsGuard.h"

using namespace spades;
using namespace spades::client;
using spades::tests::PlayerState;
using spades::tests::ReplaySnapshot;
using spades::tests::WorldSnapshot;

// NOTE: BacktraceEnvironment is already registered globally by
// Tests/Protocol/ProtocolReplayUnitTest.cpp (same test process), so any SPRaise
// (e.g. an out-of-range kill type) is catchable. Do NOT register a second one.
//
// The fixture mirrors ProtocolReplayUnitTest: SettingsGuard pins cg_unicode so
// string-bearing packets decode deterministically, plus the Push helper.
class KillLeaveFoldTest : public ::testing::Test {
protected:
	spades::tests::SettingsGuard guard_;

	// Append a writer's encoded bytes (incl. type tag at [0]) as one packet input.
	static void Push(std::vector<std::vector<char>>& packets, NetPacketWriter w) {
		packets.push_back(w.GetData());
	}

	// Build a minimal ExistingPlayer for the given id/team (rifle, weapon tool).
	static ExistingPlayerPacket MakePlayer(uint8_t id, uint8_t team) {
		ExistingPlayerPacket ep{};
		ep.playerId = id;
		ep.team = team;
		ep.weapon = 0; // rifle
		ep.tool = 2;   // weapon
		ep.score = 0;
		ep.color = MakeIntVector3(0, 0, 0);
		ep.name = "Bot";
		return ep;
	}
};

// KillAction (killType 0 = weapon, distinct killer): victim is marked dead and the
// killer's persistent score increments. Mirrors NetClient.cpp:905-909.
TEST_F(KillLeaveFoldTest, KillMarksVictimDeadAndScoresKiller) {
	KillActionPacket ka{};
	ka.victimId = 1;
	ka.killerId = 2;
	ka.killType = 0; // weapon
	ka.respawnTime = 5;

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeExistingPlayer(MakePlayer(1, 0))); // victim
	Push(packets, EncodeExistingPlayer(MakePlayer(2, 0))); // killer
	Push(packets, EncodeKillAction(ka));

	WorldSnapshot snap = ReplaySnapshot(packets, 3);

	ASSERT_EQ(snap.players.count(1), 1u);
	EXPECT_FALSE(snap.players.at(1).alive); // victim dead (NetClient.cpp:907)
	EXPECT_EQ(snap.persistentScore[2], 1);  // killer +1 (NetClient.cpp:909)
	// The killer is still alive and present.
	ASSERT_EQ(snap.players.count(2), 1u);
	EXPECT_TRUE(snap.players.at(2).alive);
}

// Self-kill (killType 4 = Fall, killerId remapped to victimId): victim dies but
// NO score is awarded (the killerId != victimId guard, NetClient.cpp:898-909).
TEST_F(KillLeaveFoldTest, SelfKillDoesNotIncrementScore) {
	KillActionPacket ka{};
	ka.victimId = 1;
	ka.killerId = 1; // Fall self-kill; the oracle remaps killer=victim anyway
	ka.killType = 4; // KillTypeFall
	ka.respawnTime = 5;

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeExistingPlayer(MakePlayer(1, 0)));
	Push(packets, EncodeKillAction(ka));

	WorldSnapshot snap = ReplaySnapshot(packets, 3);

	ASSERT_EQ(snap.players.count(1), 1u);
	EXPECT_FALSE(snap.players.at(1).alive);
	// No positive score entry for the self-killer (guard prevents over-counting).
	EXPECT_EQ(snap.persistentScore[1], 0);
}

// OQ-2: a dead player serializes health=0; a live player serializes health=100.
TEST_F(KillLeaveFoldTest, DeadPlayerSerializesHealthZero) {
	KillActionPacket ka{};
	ka.victimId = 1;
	ka.killerId = 2;
	ka.killType = 0;
	ka.respawnTime = 5;

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeExistingPlayer(MakePlayer(1, 0))); // victim → dead
	Push(packets, EncodeExistingPlayer(MakePlayer(2, 0))); // killer → alive
	Push(packets, EncodeKillAction(ka));

	nlohmann::json j = ReplaySnapshot(packets, 3).ToJson();

	int deadHealth = -1, liveHealth = -1;
	for (const auto& pj : j["players"]) {
		if (pj["id"].get<int>() == 1)
			deadHealth = pj["health"].get<int>();
		if (pj["id"].get<int>() == 2)
			liveHealth = pj["health"].get<int>();
	}
	EXPECT_EQ(deadHealth, 0);   // OQ-2: dead → 0
	EXPECT_EQ(liveHealth, 100); // alive → 100
}

// CR-02 LOAD-BEARING anti-tautology check: PlayerLeft erases the player, and a
// subsequent WorldUpdate for the SAME index must NOT resurrect them. The
// WorldUpdate reposition branch gates on players.find(index) != end()
// (ProtocolReplay.cpp), so erasure is sufficient. Mirrors NetClient.cpp:962-971.
TEST_F(KillLeaveFoldTest, PlayerLeftErasesAndPreventsResurrection) {
	PlayerLeftPacket pl{};
	pl.playerId = 3;

	// v4 WorldUpdate uses an EXPLICIT leading per-entry index, so we can target
	// index 3 directly with a single entry (v3 would need 4 filler entries).
	WorldUpdatePacket wu;
	wu.entries.push_back({3, Vector3{256.f, 256.f, 40.f}, Vector3{1.f, 0.f, 0.f}});

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeExistingPlayer(MakePlayer(3, 0))); // player 3 present + alive
	Push(packets, EncodePlayerLeft(pl));                   // player 3 leaves → erased
	Push(packets, EncodeWorldUpdate(wu, 4));               // WorldUpdate for index 3

	WorldSnapshot snap = ReplaySnapshot(packets, 4);

	// Player 3 must be ABSENT — the WorldUpdate did not re-add them (resurrection guard).
	EXPECT_EQ(snap.players.count(3), 0u);
	// Persistent score zeroed on leave (NetClient.cpp:967).
	EXPECT_EQ(snap.persistentScore[3], 0);
}
