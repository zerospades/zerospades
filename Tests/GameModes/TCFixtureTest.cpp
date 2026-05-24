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

// MODE-02 + MODE-03 (TC).
//
// MODE-02 (value_lookup, pinned clock): the TC GetProgress() formula
// (TCGameMode.cpp:40-48) is progressBasePos + progressRate*(world.GetTime() -
// progressStartTime), clamped [0,1]. To characterize the LINEAR formula (not the
// clamp), construct a TCGameMode directly (the one place RESEARCH sanctions direct
// construction over the fold), SetMode it onto a HeadlessWorld, Advance N ticks so
// World.time = N*FIXED_DT, and pick progressBasePos / progressRate / progressStartTime
// so GetProgress() lands MID-RANGE (!=0, !=1). progressRate = rate*TC_CAPTURE_RATE
// (rate is the signed wire field). A second value block captures the post-capture
// reset state (ownerTeamId=state, progressRate=0, capturingTeamId=-1) folded from a
// TerritoryCapture packet (NetClient.cpp:972-1007).
//
// MODE-03 (world_snapshot): one full TC packet sequence (StateData mode!=0 +
// ExistingPlayers + TerritoryCapture) folded once -> WorldSnapshot::ToJson() frozen
// with game_mode.mode == "tc".
//
// All replay assertions read expectations from the frozen JSON (SCHE-06 portability).
//
// NOTE: BacktraceEnvironment is registered globally by ProtocolReplayUnitTest.cpp
// (same test process). Do NOT register another.

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <Client/GameConstants.h> // TC_CAPTURE_RATE
#include <Client/IGameMode.h>
#include <Client/ProtocolCodec.h>
#include <Client/TCGameMode.h>
#include <Client/World.h>
#include <Core/Math.h>

#include "../Protocol/ProtocolReplay.h"
#include "GameModeTestBase.h"
#include "HeadlessWorld.h"
#include "MakeFlatMap.h"

using namespace spades;
using namespace spades::client;
using spades::tests::BuildModeFixtureEnvelope;
using spades::tests::ExpectSnapshotMatches;
using spades::tests::FIXED_DT;
using spades::tests::GameModeTestBase;
using spades::tests::HeadlessWorld;
using spades::tests::LoadModeFixtureJson;
using spades::tests::ReplaySnapshot;
using spades::tests::WorldSnapshot;
using spades::tests::WriteModeFixture;

namespace {

	// Pinned-clock recipe constants. After kTicks ticks World.time = kTicks*FIXED_DT
	// = 120/60 = 2.0 s. With base 0.2 + rate(0.15)*2.0 = 0.5 → mid-range (clamp not hit).
	constexpr int kTicks = 120;
	constexpr float kProgressBasePos = 0.2F;
	constexpr int8_t kRate = 3; // progressRate = 3 * TC_CAPTURE_RATE(0.05) = 0.15
	constexpr float kProgressStartTime = 0.0F;
	// Post-capture reset packet fields.
	constexpr uint8_t kCaptureTerritoryId = 0;
	constexpr uint8_t kCaptureState = 1; // new owner team

	void Push(std::vector<std::vector<char>>& packets, NetPacketWriter w) {
		packets.push_back(w.GetData());
	}

	// Build + drive the pinned-clock TCGameMode; return GetProgress() and the world
	// time used. The territory fields are set to the recipe constants. The TCGameMode
	// is owned by the World (SetMode moves it); `tc` is a retained non-owning pointer.
	struct PinnedResult {
		float progress;
		float worldTime;
	};
	PinnedResult RunPinnedProgress() {
		auto vxl = spades::tests::MakeFlatMapBytes();
		HeadlessWorld hw(/*seed*/ 42, vxl);
		World& world = hw.GetWorld();

		TCGameMode* tc = new TCGameMode(world);
		world.SetMode(std::unique_ptr<IGameMode>(tc));

		TCGameMode::Territory t(*tc);
		t.pos = Vector3{128.0F, 128.0F, 40.0F};
		t.ownerTeamId = 2;     // neutral
		t.capturingTeamId = 0; // team 0 capturing
		t.progressBasePos = kProgressBasePos;
		t.progressRate = (float)kRate * TC_CAPTURE_RATE;
		t.progressStartTime = kProgressStartTime;
		tc->AddTerritory(t);

		hw.Advance(kTicks); // World.time = kTicks * FIXED_DT

		PinnedResult r;
		r.worldTime = world.GetTime();
		r.progress = tc->GetTerritory(0).GetProgress();
		return r;
	}

	// Fold a single TerritoryCapture packet and return the reset territory state.
	WorldSnapshot::TerritoryState FoldCaptureReset() {
		std::vector<std::vector<char>> packets;
		TerritoryCapturePacket tcap{};
		tcap.territoryId = kCaptureTerritoryId;
		tcap.winning = 0;
		tcap.state = kCaptureState;
		Push(packets, EncodeTerritoryCapture(tcap));
		WorldSnapshot snap = ReplaySnapshot(packets, 3);
		return snap.territories.at(kCaptureTerritoryId);
	}

	// The fixed TC world-golden sequence: StateData(TC) + two ExistingPlayers +
	// TerritoryCapture.
	void PushTcWorldSequence(std::vector<std::vector<char>>& packets) {
		StateDataPacket sd{};
		sd.playerId = 0;
		sd.mode = 1; // TC (non-zero)
		sd.teamName[0] = "Red";
		sd.teamName[1] = "Blue";
		StateDataPacket::Territory terr{};
		terr.pos = Vector3{128.0F, 128.0F, 40.0F};
		terr.state = 2; // neutral
		sd.tcTerritories.push_back(terr);
		Push(packets, EncodeStateData(sd));

		ExistingPlayerPacket p1{};
		p1.playerId = 1;
		p1.team = 0;
		p1.weapon = 0; // rifle
		p1.tool = 2;   // weapon
		p1.score = 0;
		p1.color = MakeIntVector3(0, 0, 0);
		p1.name = "RedOne";
		Push(packets, EncodeExistingPlayer(p1));

		ExistingPlayerPacket p2{};
		p2.playerId = 2;
		p2.team = 1;
		p2.weapon = 1; // smg
		p2.tool = 2;   // weapon
		p2.score = 0;
		p2.color = MakeIntVector3(0, 0, 0);
		p2.name = "BlueOne";
		Push(packets, EncodeExistingPlayer(p2));

		TerritoryCapturePacket tcap{};
		tcap.territoryId = 0;
		tcap.winning = 0;
		tcap.state = 0; // captured by team 0
		Push(packets, EncodeTerritoryCapture(tcap));
	}

} // namespace

// ---------------------------------------------------------------------------
// MODE-02 generator: pinned-clock GetProgress (mid-range) + post-capture reset.
// ---------------------------------------------------------------------------

TEST(DISABLED_TCFixtureGenerate, PinnedProgress) {
	spades::tests::SettingsGuard guard;

	PinnedResult pin = RunPinnedProgress();
	// Sanity: the recipe must land mid-range so the LINEAR formula is exercised
	// (not the [0,1] clamp). Abort the generator loudly if not.
	ASSERT_GT(pin.progress, 0.0F) << "pinned progress hit the lower clamp";
	ASSERT_LT(pin.progress, 1.0F) << "pinned progress hit the upper clamp";

	WorldSnapshot::TerritoryState reset = FoldCaptureReset();

	nlohmann::json j = BuildModeFixtureEnvelope("mode_value_lookup_tc_progress");
	nlohmann::json& v = j["expected"]["value"];
	v["pinned"] = {{"world_time_s", (double)pin.worldTime},
	               {"progress_base_pos", (double)kProgressBasePos},
	               {"progress_rate", (double)((float)kRate * TC_CAPTURE_RATE)},
	               {"progress_start_time", (double)kProgressStartTime},
	               {"progress", (double)pin.progress}};
	v["reset"] = {{"owner_team_id", reset.ownerTeamId},
	              {"capturing_team_id", reset.capturingTeamId},
	              {"progress_base_pos", (double)reset.progressBasePos},
	              {"progress_rate", (double)reset.progressRate},
	              {"progress_start_time", (double)reset.progressStartTime}};
	WriteModeFixture("mode_value_lookup_tc_progress.json", j);
}

// MODE-02 replay: reconstruct the identical pinned scenario + capture reset and
// assert against the frozen JSON. Floats via tolerance, ints exact.
TEST_F(GameModeTestBase, TCPinnedProgress) {
	nlohmann::json j = LoadFixtureJson("mode_value_lookup_tc_progress.json");
	const nlohmann::json& pinJ = j["expected"]["value"]["pinned"];
	const nlohmann::json& resetJ = j["expected"]["value"]["reset"];

	// Frozen progress must be mid-range (read from JSON, not recomputed here).
	double frozenProgress = pinJ["progress"].get<double>();
	EXPECT_GT(frozenProgress, 0.0) << "frozen pinned progress must not be 0 (clamp)";
	EXPECT_LT(frozenProgress, 1.0) << "frozen pinned progress must not be 1 (clamp)";

	PinnedResult pin = RunPinnedProgress();
	EXPECT_NEAR(pin.worldTime, pinJ["world_time_s"].get<double>(), 1e-4);
	EXPECT_NEAR(pin.progress, frozenProgress, spades::tests::POSITION_TOL);

	WorldSnapshot::TerritoryState reset = FoldCaptureReset();
	EXPECT_EQ(reset.ownerTeamId, resetJ["owner_team_id"].get<int>());
	EXPECT_EQ(reset.capturingTeamId, resetJ["capturing_team_id"].get<int>());
	EXPECT_NEAR(reset.progressBasePos, resetJ["progress_base_pos"].get<double>(), 1e-4);
	EXPECT_NEAR(reset.progressRate, resetJ["progress_rate"].get<double>(), 1e-4);
	EXPECT_NEAR(reset.progressStartTime, resetJ["progress_start_time"].get<double>(), 1e-4);

	// Anti-tautology on the frozen reset semantics (NetClient.cpp:999-1003).
	EXPECT_EQ(resetJ["progress_rate"].get<double>(), 0.0) << "capture zeroes progressRate";
	EXPECT_EQ(resetJ["capturing_team_id"].get<int>(), -1) << "capture clears capturingTeamId";
	EXPECT_EQ(resetJ["owner_team_id"].get<int>(), (int)kCaptureState) << "capture sets new owner";
}

// ---------------------------------------------------------------------------
// MODE-03 generator: thin TC world_snapshot golden (game_mode.mode == "tc").
// ---------------------------------------------------------------------------

TEST(DISABLED_TCFixtureGenerate, WorldGolden) {
	spades::tests::SettingsGuard guard;

	std::vector<std::vector<char>> packets;
	PushTcWorldSequence(packets);

	WorldSnapshot snap = ReplaySnapshot(packets, 3);
	nlohmann::json expected = snap.ToJson();
	ASSERT_TRUE(expected.contains("game_mode"));
	ASSERT_EQ(expected["game_mode"]["mode"].get<std::string>(), "tc");

	nlohmann::json j =
	  BuildModeFixtureEnvelope("mode_world_snapshot_tc", "world_snapshot", "protocol_compat");
	j["expected"] = expected;
	WriteModeFixture("mode_world_snapshot_tc.json", j);
}

// MODE-03 replay: re-fold + field-compare against the frozen golden.
TEST_F(GameModeTestBase, TCWorldGolden) {
	nlohmann::json j = LoadFixtureJson("mode_world_snapshot_tc.json");
	const nlohmann::json& want = j["expected"];
	EXPECT_EQ(want["game_mode"]["mode"].get<std::string>(), "tc");

	std::vector<std::vector<char>> packets;
	PushTcWorldSequence(packets);

	WorldSnapshot snap = ReplaySnapshot(packets, 3);
	ExpectSnapshotMatches(want, snap.ToJson(), "tc");
}
