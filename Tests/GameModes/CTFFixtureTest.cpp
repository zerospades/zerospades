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

// MODE-01 + MODE-03 (CTF). Driven through the extended ProtocolReplay fold.
//
// Pattern: generate-then-freeze (value_lookup + world_snapshot). The current C++
// NetClient CTF semantics (mirrored by the fold, NetClient.cpp:1041-1110) are the
// oracle. The generator builds a CTF StateData (mode=0) + two ExistingPlayers, then
// a scripted intel sequence [Pickup(1) -> Drop(1,pos) -> Pickup(1) -> Capture(1,win)].
// After EACH packet the cumulative prefix is folded via ReplaySnapshot and the CTF
// team state is sampled into expected.value.samples[] — proving every transition
// (hasIntel toggle, carrierId set, score++, flagPos on drop, winning reset), not just
// the terminal state (Nyquist; CONTEXT OQ-1 / VALIDATION per-packet CTF sampling).
//
// MODE-03: one full CTF sequence folded once -> WorldSnapshot::ToJson() frozen as a
// thin world_snapshot golden carrying game_mode.mode == "ctf".
//
// NO GameModeSnapshot schema change: CTF team state is read DIRECTLY off the
// WorldSnapshot ctf* members (NOT serialized into ToJson), per OQ-1 (locked
// value_lookup). All replay assertions read expectations from the frozen JSON.
//
// NOTE: BacktraceEnvironment is already registered globally by
// Tests/Protocol/ProtocolReplayUnitTest.cpp (same test process), so any SPRaise
// (e.g. a bad weapon/tool enum in the fold) is catchable. Do NOT register another.

#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <Client/ProtocolCodec.h>
#include <Core/Math.h>

#include "../Protocol/ProtocolReplay.h"
#include "GameModeTestBase.h"

using namespace spades;
using namespace spades::client;
using spades::tests::BuildModeFixtureEnvelope;
using spades::tests::ExpectSnapshotMatches;
using spades::tests::GameModeTestBase;
using spades::tests::LoadModeFixtureJson;
using spades::tests::ReplaySnapshot;
using spades::tests::WorldSnapshot;
using spades::tests::WriteModeFixture;

namespace {

	// CTF golden constants. Player 1 = team 0 (carrier), player 2 = team 1.
	constexpr int kCarrierId = 1;
	constexpr int kCarrierTeam = 0;
	constexpr int kOtherId = 2;
	constexpr int kOtherTeam = 1;
	const Vector3 kDropPos{200.0F, 150.0F, 40.0F};

	// Append a writer's encoded bytes (incl. type tag at [0]) as one packet input.
	void Push(std::vector<std::vector<char>>& packets, NetPacketWriter w) {
		packets.push_back(w.GetData());
	}

	// The fixed CTF setup prefix: StateData(CTF) + two ExistingPlayers.
	void PushSetup(std::vector<std::vector<char>>& packets) {
		StateDataPacket sd{};
		sd.playerId = 0;
		sd.mode = 0; // CTF
		sd.teamName[0] = "Blue";
		sd.teamName[1] = "Green";
		sd.ctfCaptureLimit = 10;
		Push(packets, EncodeStateData(sd));

		ExistingPlayerPacket p1{};
		p1.playerId = kCarrierId;
		p1.team = kCarrierTeam;
		p1.weapon = 0; // rifle
		p1.tool = 2;   // weapon
		p1.score = 0;
		p1.color = MakeIntVector3(0, 0, 0);
		p1.name = "Carrier";
		Push(packets, EncodeExistingPlayer(p1));

		ExistingPlayerPacket p2{};
		p2.playerId = kOtherId;
		p2.team = kOtherTeam;
		p2.weapon = 1; // smg
		p2.tool = 2;   // weapon
		p2.score = 0;
		p2.color = MakeIntVector3(0, 0, 0);
		p2.name = "Other";
		Push(packets, EncodeExistingPlayer(p2));
	}

	// Append the i-th intel packet (0..3) to `packets`. The scripted sequence is
	// Pickup(1) -> Drop(1,pos) -> Pickup(1) -> Capture(1, winning=0).
	void PushIntelStep(std::vector<std::vector<char>>& packets, int step) {
		switch (step) {
			case 0: {
				IntelPickupPacket ip{};
				ip.playerId = kCarrierId;
				Push(packets, EncodeIntelPickup(ip));
			} break;
			case 1: {
				IntelDropPacket id{};
				id.playerId = kCarrierId;
				id.position = kDropPos;
				Push(packets, EncodeIntelDrop(id));
			} break;
			case 2: {
				IntelPickupPacket ip{};
				ip.playerId = kCarrierId;
				Push(packets, EncodeIntelPickup(ip));
			} break;
			case 3: {
				IntelCapturePacket ic{};
				ic.playerId = kCarrierId;
				ic.winning = 0; // non-winning: score++ but no ResetIntelHoldingStatus
				Push(packets, EncodeIntelCapture(ic));
			} break;
			default: break;
		}
	}

	const char* kStepLabel[4] = {"pickup1", "drop1", "pickup2", "capture1"};
	constexpr int kNumSteps = 4;

	// Serialize the CTF team state from a folded snapshot into a sample object.
	nlohmann::json SampleCtfState(const WorldSnapshot& snap, const std::string& after) {
		nlohmann::json s;
		s["after"] = after;
		s["team0_score"] = (int)snap.ctfScore[0];
		s["team1_score"] = (int)snap.ctfScore[1];
		s["team0_hasIntel"] = snap.ctfHasIntel[0];
		s["team1_hasIntel"] = snap.ctfHasIntel[1];
		s["team0_carrier"] = (int)snap.ctfCarrierId[0];
		s["team1_carrier"] = (int)snap.ctfCarrierId[1];
		s["team0_flag"] = {{"x", (double)snap.ctfFlagPos[0].x},
		                   {"y", (double)snap.ctfFlagPos[0].y},
		                   {"z", (double)snap.ctfFlagPos[0].z}};
		s["team1_flag"] = {{"x", (double)snap.ctfFlagPos[1].x},
		                   {"y", (double)snap.ctfFlagPos[1].y},
		                   {"z", (double)snap.ctfFlagPos[1].z}};
		auto carrierScore = snap.persistentScore.find(kCarrierId);
		s["carrier_persistent"] =
		  carrierScore != snap.persistentScore.end() ? carrierScore->second : 0;
		return s;
	}

} // namespace

// ---------------------------------------------------------------------------
// MODE-01 generator: per-packet CTF intel-state value_lookup.
// Build the setup prefix, then fold the cumulative prefix after EACH intel
// packet and snapshot the CTF team state. Frozen as expected.value.samples[].
// ---------------------------------------------------------------------------

TEST(DISABLED_CTFFixtureGenerate, IntelSequence) {
	spades::tests::SettingsGuard guard;

	std::vector<std::vector<char>> packets;
	PushSetup(packets);

	nlohmann::json samples = nlohmann::json::array();
	for (int step = 0; step < kNumSteps; step++) {
		PushIntelStep(packets, step);
		WorldSnapshot snap = ReplaySnapshot(packets, 3);
		samples.push_back(SampleCtfState(snap, kStepLabel[step]));
	}

	nlohmann::json j = BuildModeFixtureEnvelope("mode_value_lookup_ctf_sequence");
	j["expected"]["value"]["samples"] = samples;
	j["expected"]["value"]["carrier_id"] = kCarrierId;
	j["expected"]["value"]["carrier_team"] = kCarrierTeam;
	WriteModeFixture("mode_value_lookup_ctf_sequence.json", j);
}

// MODE-01 replay: re-fold each prefix and assert every sampled field matches the
// frozen JSON. Reads all expectations from the fixture (SCHE-06 portability).
TEST_F(GameModeTestBase, CTFIntelSequence) {
	nlohmann::json j = LoadFixtureJson("mode_value_lookup_ctf_sequence.json");
	const nlohmann::json& samples = j["expected"]["value"]["samples"];
	ASSERT_EQ(samples.size(), (size_t)kNumSteps);

	std::vector<std::vector<char>> packets;
	PushSetup(packets);

	for (int step = 0; step < kNumSteps; step++) {
		PushIntelStep(packets, step);
		WorldSnapshot snap = ReplaySnapshot(packets, 3);
		nlohmann::json got = SampleCtfState(snap, kStepLabel[step]);
		// Recursive compare reads the frozen expectation (ints exact, flag floats
		// via tolerance) — no hardcoded oracle value in the assertion body.
		ExpectSnapshotMatches(samples[step], got, std::string("samples[") +
		                                             std::to_string(step) + "]");
	}

	// Anti-tautology: the frozen samples must actually SHOW each transition, so a
	// no-op fold (all zeros) cannot pass. These read the frozen JSON, not the fold.
	EXPECT_TRUE(samples[0]["team0_hasIntel"].get<bool>()) << "pickup must set hasIntel";
	EXPECT_EQ(samples[0]["team0_carrier"].get<int>(), kCarrierId) << "pickup sets carrier";
	EXPECT_FALSE(samples[1]["team0_hasIntel"].get<bool>()) << "drop clears hasIntel";
	// Drop sets team(1-teamId).flagPos = position; carrier is team 0 -> team1_flag.
	EXPECT_NEAR(samples[1]["team1_flag"]["x"].get<double>(), kDropPos.x, 1e-4);
	EXPECT_NEAR(samples[1]["team1_flag"]["y"].get<double>(), kDropPos.y, 1e-4);
	EXPECT_NEAR(samples[1]["team1_flag"]["z"].get<double>(), kDropPos.z, 1e-4);
	EXPECT_TRUE(samples[2]["team0_hasIntel"].get<bool>()) << "second pickup re-sets hasIntel";
	EXPECT_EQ(samples[3]["team0_score"].get<int>(), 1) << "capture increments team score";
	EXPECT_FALSE(samples[3]["team0_hasIntel"].get<bool>()) << "capture clears hasIntel";
	EXPECT_EQ(samples[3]["carrier_persistent"].get<int>(), 10) << "capture awards +10 persistent";
}

// ---------------------------------------------------------------------------
// MODE-03 generator: thin CTF world_snapshot golden. Fold a full CTF sequence
// once; WorldSnapshot::ToJson() carries game_mode.mode == "ctf".
// ---------------------------------------------------------------------------

TEST(DISABLED_CTFFixtureGenerate, WorldGolden) {
	spades::tests::SettingsGuard guard;

	std::vector<std::vector<char>> packets;
	PushSetup(packets);
	IntelPickupPacket ip{};
	ip.playerId = kCarrierId;
	Push(packets, EncodeIntelPickup(ip));

	WorldSnapshot snap = ReplaySnapshot(packets, 3);
	nlohmann::json expected = snap.ToJson();
	ASSERT_TRUE(expected.contains("game_mode"));
	ASSERT_EQ(expected["game_mode"]["mode"].get<std::string>(), "ctf");

	nlohmann::json j =
	  BuildModeFixtureEnvelope("mode_world_snapshot_ctf", "world_snapshot", "protocol_compat");
	j["expected"] = expected;
	WriteModeFixture("mode_world_snapshot_ctf.json", j);
}

// MODE-03 replay: re-fold the identical sequence and field-compare against the
// frozen world_snapshot golden.
TEST_F(GameModeTestBase, CTFWorldGolden) {
	nlohmann::json j = LoadFixtureJson("mode_world_snapshot_ctf.json");
	const nlohmann::json& want = j["expected"];
	EXPECT_EQ(want["game_mode"]["mode"].get<std::string>(), "ctf");

	std::vector<std::vector<char>> packets;
	PushSetup(packets);
	IntelPickupPacket ip{};
	ip.playerId = kCarrierId;
	Push(packets, EncodeIntelPickup(ip));

	WorldSnapshot snap = ReplaySnapshot(packets, 3);
	ExpectSnapshotMatches(want, snap.ToJson(), "ctf");
}
