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

// PHYS-03: 60-tick multi-step determinism golden test.
//
// Pattern: generate-then-freeze.
//   - DISABLED_PhysDeterminismGenerate.DeterminismGolden_DISABLED emits the
//     60-tick fixture from the C++ oracle. Run manually once, commit the frozen
//     fixture, then never again.
//   - PhysicsTestBase.PhysDeterminismGolden replays the frozen fixture tick-by-tick
//     via ExpectSnapshotMatches (tolerance — never EXPECT_FLOAT_EQ).
//   - PhysicsTestBase.PhysDeterminismRepeat re-runs the identical simulation 20 times
//     and asserts all runs produce nlohmann::json-equal tick-0 snapshots (bitwise
//     determinism proof — no wall-clock, no RNG, no Settings reads in the physics path).

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <Client/GameConstants.h>
#include <Client/GameMap.h>
#include <Client/Player.h>
#include <Client/World.h>

#include "HeadlessWorld.h"
#include "MakeFlatMap.h"
#include "PhysicsTestBase.h"
#include "PlayerPhysicsSnapshot.h"
#include "SettingsGuard.h"

using namespace spades;
using namespace spades::tests;
using namespace spades::client;

namespace {

	// ---------------------------------------------------------------------------
	// InputForTick: encode the scripted 60-tick input schedule.
	//
	// Used by both the generator and the replay test to guarantee identical inputs.
	// The schedule exercises move, jump, crouch, and strafe transitions:
	//   Ticks  0- 9: moveForward only
	//   Ticks 10-14: jump (first press)
	//   Ticks 15-19: moveForward + moveRight
	//   Ticks 20-24: crouch + moveForward
	//   Ticks 25-29: moveLeft only
	//   Ticks 30-39: sprint + moveForward
	//   Ticks 40-44: jump (second press; back in air after sprint)
	//   Ticks 45-49: all false (coast)
	//   Ticks 50-54: moveBackward only
	//   Ticks 55-59: all false (coast to stop)
	//
	// Orientation is fixed (1,0,0) throughout — set once at spawn, not via input.
	// ---------------------------------------------------------------------------
	PlayerInput InputForTick(int tick) {
		PlayerInput inp;
		if (tick >= 0 && tick <= 9) {
			inp.moveForward = true;
		} else if (tick >= 10 && tick <= 14) {
			inp.jump = true;
		} else if (tick >= 15 && tick <= 19) {
			inp.moveForward = true;
			inp.moveRight = true;
		} else if (tick >= 20 && tick <= 24) {
			inp.crouch = true;
			inp.moveForward = true;
		} else if (tick >= 25 && tick <= 29) {
			inp.moveLeft = true;
		} else if (tick >= 30 && tick <= 39) {
			inp.sprint = true;
			inp.moveForward = true;
		} else if (tick >= 40 && tick <= 44) {
			inp.jump = true;
		} else if (tick >= 45 && tick <= 49) {
			// all false — coast
		} else if (tick >= 50 && tick <= 54) {
			inp.moveBackward = true;
		} else {
			// ticks 55-59: all false — coast to stop
		}
		return inp;
	}

	// ---------------------------------------------------------------------------
	// SpawnDeterminismPlayer: construct Player 0 at the determinism golden spawn.
	// Returns a raw pointer valid as long as the HeadlessWorld lives.
	// ---------------------------------------------------------------------------
	client::Player* SpawnDeterminismPlayer(HeadlessWorld& hw) {
		Vector3 pos = {256.0F, 256.0F, 60.5F};
		Vector3 vel = {0.0F, 0.0F, 0.0F};
		Vector3 ori = {1.0F, 0.0F, 0.0F};
		auto player =
		  std::make_unique<client::Player>(hw.GetWorld(), 0, WeaponType::RIFLE_WEAPON, 0);
		player->SetPosition(pos);
		player->SetVelocity(vel);
		player->SetOrientation(ori);
		hw.GetWorld().SetPlayer(0, std::move(player));
		hw.GetWorld().SetLocalPlayerIndex(0);
		auto opt = hw.GetWorld().GetPlayer(0);
		return opt ? &*opt : nullptr;
	}

	// ---------------------------------------------------------------------------
	// RunDeterminism60Ticks: run the 60-tick scripted sequence and capture ticks.
	// Returns a JSON array of 60 tick objects.
	// ---------------------------------------------------------------------------
	nlohmann::json RunDeterminism60Ticks(HeadlessWorld& hw, client::Player* p) {
		nlohmann::json ticks = nlohmann::json::array();
		for (int i = 0; i < 60; i++) {
			p->SetInput(InputForTick(i));
			hw.Advance(1);
			auto opt = hw.GetWorld().GetPlayer(0);
			EXPECT_TRUE(opt != nullptr) << "Player 0 gone at tick " << i;
			if (opt)
				ticks.push_back(SnapshotPlayerTick(*opt, i));
		}
		return ticks;
	}

	// ---------------------------------------------------------------------------
	// BuildDeterminismEnvelope: assemble the determinism fixture JSON.
	// ---------------------------------------------------------------------------
	nlohmann::json BuildDeterminismEnvelope(const nlohmann::json& ticks) {
		nlohmann::json j;
		j["version"] = "1.0.0";
		j["id"] = "phys_determinism_golden_001";
		j["subsystem"] = "phys";
		j["behavior"] = "implementation_detail";
		j["seed"] = 42;
		j["protocol_version"] = "0.75";
		j["map"] = {{"generator", "flat"}, {"ground_z", 62}};
		j["inputs"] = nlohmann::json::array();
		j["kind"] = "step_trace";
		j["expected"] = {{"ticks", ticks}};
		return j;
	}

} // namespace

// ===========================================================================
// DISABLED_ generator — run manually once, commit the frozen fixture.
// ===========================================================================

TEST(DISABLED_PhysDeterminismGenerate, DeterminismGolden_DISABLED) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto* p = SpawnDeterminismPlayer(hw);
	ASSERT_NE(p, nullptr);

	auto ticks = RunDeterminism60Ticks(hw, p);
	ASSERT_EQ(60u, ticks.size()) << "Expected exactly 60 tick entries";

	std::string path = std::string(TESTS_DIR) + "/../fixtures/phys_determinism_golden_001.json";
	std::ofstream f(path);
	ASSERT_TRUE(f.is_open()) << "Cannot open for write: " << path;
	auto envelope = BuildDeterminismEnvelope(ticks);
	f << envelope.dump(4) << "\n";
}

// ===========================================================================
// Replay test — load frozen 60-tick golden, re-run, compare tick-by-tick.
// ===========================================================================

TEST_F(PhysicsTestBase, PhysDeterminismGolden) {
	auto fixture = LoadFixtureJson("phys_determinism_golden_001.json");
	const auto& want_ticks = fixture.at("expected").at("ticks");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto* p = SpawnDeterminismPlayer(hw);
	ASSERT_NE(p, nullptr);

	auto got_ticks = RunDeterminism60Ticks(hw, p);

	ASSERT_EQ(60u, got_ticks.size()) << "Expected exactly 60 ticks from replay";
	ASSERT_EQ(want_ticks.size(), got_ticks.size()) << "Frozen fixture tick count mismatch";

	for (size_t i = 0; i < want_ticks.size(); i++) {
		ExpectSnapshotMatches(want_ticks[i], got_ticks[i],
		                      "expected.ticks[" + std::to_string(i) + "]");
	}
}

// ===========================================================================
// Determinism repeat test — 20 identical runs must produce JSON-equal tick-0
// snapshots (in-process CI-stability proof; no wall-clock, no RNG in physics path).
// ===========================================================================

TEST_F(PhysicsTestBase, PhysDeterminismRepeat) {
	constexpr int kRuns = 20;
	std::vector<nlohmann::json> tick0_snapshots;
	tick0_snapshots.reserve(kRuns);

	for (int run = 0; run < kRuns; run++) {
		auto vxl = MakeFlatMapBytes();
		HeadlessWorld hw(42, vxl);
		auto* p = SpawnDeterminismPlayer(hw);
		ASSERT_NE(p, nullptr) << "run " << run << ": SpawnDeterminismPlayer returned null";

		auto ticks = RunDeterminism60Ticks(hw, p);
		ASSERT_FALSE(ticks.empty()) << "run " << run << ": no ticks produced";
		tick0_snapshots.push_back(ticks[0]);
	}

	ASSERT_EQ(kRuns, static_cast<int>(tick0_snapshots.size()));
	for (int j = 1; j < kRuns; j++) {
		EXPECT_EQ(tick0_snapshots[0], tick0_snapshots[j])
		  << "run " << j << " tick-0 snapshot differs from run 0 (determinism failure)";
	}
}
