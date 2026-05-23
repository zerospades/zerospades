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

// PHYS-01 per-behavior step-trace fixture tests.
//
// Pattern: generate-then-freeze.
//   - DISABLED_PhysicsGenerate.* tests emit expected.ticks from the C++ oracle.
//     Run manually once, commit the frozen fixture, then never again.
//   - PhysicsFixtureTest.* replay tests load the frozen fixture, re-run the
//     identical scenario in-process, and assert match via ExpectSnapshotMatches
//     (EXPECT_NEAR tolerance — never EXPECT_FLOAT_EQ).

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
	// SpawnPlayer: construct Player 0 and register it as the local player.
	// Returns a raw pointer valid as long as the HeadlessWorld lives.
	// ---------------------------------------------------------------------------
	client::Player* SpawnPlayer(HeadlessWorld& hw, Vector3 pos, Vector3 vel, Vector3 ori) {
		auto player =
		  std::make_unique<client::Player>(hw.GetWorld(), 0, WeaponType::RIFLE_WEAPON, 0);
		player->SetPosition(pos);
		player->SetVelocity(vel);
		player->SetOrientation(ori);
		hw.GetWorld().SetPlayer(0, std::move(player));
		hw.GetWorld().SetLocalPlayerIndex(0);
		// GetPlayer returns a Handle; dereference to raw pointer.
		auto opt = hw.GetWorld().GetPlayer(0);
		return opt ? &*opt : nullptr;
	}

	// ---------------------------------------------------------------------------
	// RunAndCaptureTicks: advance `n` ticks capturing a snapshot after each tick.
	// Returns a JSON array of tick objects (tick=0..n-1).
	// ---------------------------------------------------------------------------
	nlohmann::json RunAndCaptureTicks(HeadlessWorld& hw, int n) {
		nlohmann::json ticks = nlohmann::json::array();
		for (int i = 0; i < n; i++) {
			hw.Advance(1);
			auto opt = hw.GetWorld().GetPlayer(0);
			EXPECT_TRUE(opt != nullptr) << "Player 0 gone at tick " << i;
			if (opt)
				ticks.push_back(SnapshotPlayerTick(*opt, i));
		}
		return ticks;
	}

	// ---------------------------------------------------------------------------
	// BuildFixtureEnvelope: assemble the full fixture JSON with frozen metadata.
	// ---------------------------------------------------------------------------
	nlohmann::json BuildFixtureEnvelope(const std::string& id, const nlohmann::json& ticks) {
		nlohmann::json j;
		j["version"] = "1.0.0";
		j["id"] = id;
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

	// ---------------------------------------------------------------------------
	// WriteFixture: serialize fixture envelope to the fixtures/ directory.
	// ---------------------------------------------------------------------------
	void WriteFixture(const std::string& name, const nlohmann::json& j) {
		std::string path = std::string(TESTS_DIR) + "/../fixtures/" + name;
		std::ofstream f(path);
		ASSERT_TRUE(f.is_open()) << "Cannot open for write: " << path;
		f << j.dump(4) << "\n";
	}

} // namespace

// ===========================================================================
// DISABLED_ generator tests — run manually once, then commit frozen fixture.
// ===========================================================================

// Ground friction: player at (256,256,60.5) with vx=1.0, zero input, 10 ticks.
// f = fsynctics*4+1 per tick on !airborne && !wade branch.
TEST(DISABLED_PhysicsGenerate, GroundFriction) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	SpawnPlayer(hw, {256.0F, 256.0F, 60.5F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
	auto ticks = RunAndCaptureTicks(hw, 10);
	WriteFixture("phys_friction_ground_001.json",
	             BuildFixtureEnvelope("phys_friction_ground_001", ticks));
}

// Air friction: player at (256,256,32.0) with vx=1.0, zero input, 5 ticks.
// No ground nearby; airborne=true; f = fsynctics+1 (weakest).
TEST(DISABLED_PhysicsGenerate, AirFriction) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	SpawnPlayer(hw, {256.0F, 256.0F, 32.0F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
	auto ticks = RunAndCaptureTicks(hw, 5);
	WriteFixture("phys_friction_air_001.json",
	             BuildFixtureEnvelope("phys_friction_air_001", ticks));
}

// Wade friction: player at (256,256,62.5) with vx=1.0, zero input, 10 ticks.
// position.z > 61 → wade=true; f = fsynctics*6+1 (strongest).
TEST(DISABLED_PhysicsGenerate, WadeFriction) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	SpawnPlayer(hw, {256.0F, 256.0F, 62.5F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
	auto ticks = RunAndCaptureTicks(hw, 10);
	WriteFixture("phys_friction_wade_001.json",
	             BuildFixtureEnvelope("phys_friction_wade_001", ticks));
}

// Jump: player on flat ground at (256,256,60.5), jump=true tick 0, 3 ticks.
// Expected: velocity.z = -0.36 at tick 0 (PlayerJump constant).
TEST(DISABLED_PhysicsGenerate, Jump) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto* p = SpawnPlayer(hw, {256.0F, 256.0F, 60.5F}, {0.0F, 0.0F, 0.0F},
	                      {1.0F, 0.0F, 0.0F});
	ASSERT_NE(p, nullptr);

	nlohmann::json ticks = nlohmann::json::array();
	for (int i = 0; i < 3; i++) {
		PlayerInput inp;
		inp.jump = (i == 0);
		p->SetInput(inp);
		hw.Advance(1);
		auto opt = hw.GetWorld().GetPlayer(0);
		ASSERT_NE(opt, nullptr);
		ticks.push_back(SnapshotPlayerTick(*opt, i));
	}
	WriteFixture("phys_jump_001.json", BuildFixtureEnvelope("phys_jump_001", ticks));
}

// Jump cooldown: jump tick 0, release tick 1, re-jump tick 2.
// Cooldown only gates PlayerJumped listener; velocity.z=-0.36 fires at both tick 0 and 2.
TEST(DISABLED_PhysicsGenerate, JumpCooldown) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto* p = SpawnPlayer(hw, {256.0F, 256.0F, 60.5F}, {0.0F, 0.0F, 0.0F},
	                      {1.0F, 0.0F, 0.0F});
	ASSERT_NE(p, nullptr);

	// tick 0: jump=true, tick 1: jump=false, tick 2: jump=true
	bool jumpSeq[3] = {true, false, true};
	nlohmann::json ticks = nlohmann::json::array();
	for (int i = 0; i < 3; i++) {
		PlayerInput inp;
		inp.jump = jumpSeq[i];
		p->SetInput(inp);
		hw.Advance(1);
		auto opt = hw.GetWorld().GetPlayer(0);
		ASSERT_NE(opt, nullptr);
		ticks.push_back(SnapshotPlayerTick(*opt, i));
	}
	WriteFixture("phys_jump_cooldown_001.json",
	             BuildFixtureEnvelope("phys_jump_cooldown_001", ticks));
}

// Crouch offset: player standing at (256,256,60.5), crouch=true tick 0, 3 ticks.
// position.z increases by 0.9 on crouch-down transition (SetInput:104-121).
TEST(DISABLED_PhysicsGenerate, CrouchOffset) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto* p = SpawnPlayer(hw, {256.0F, 256.0F, 60.5F}, {0.0F, 0.0F, 0.0F},
	                      {1.0F, 0.0F, 0.0F});
	ASSERT_NE(p, nullptr);

	nlohmann::json ticks = nlohmann::json::array();
	for (int i = 0; i < 3; i++) {
		PlayerInput inp;
		inp.crouch = true;
		p->SetInput(inp);
		hw.Advance(1);
		auto opt = hw.GetWorld().GetPlayer(0);
		ASSERT_NE(opt, nullptr);
		ticks.push_back(SnapshotPlayerTick(*opt, i));
	}
	WriteFixture("phys_crouch_offset_001.json",
	             BuildFixtureEnvelope("phys_crouch_offset_001", ticks));
}

// Step-climb: 1-block wall at (258,256,61), player at (256,256,60.5) moving +x, 5 ticks.
// Climb fires: velocity.x/y halved, nz--.
TEST(DISABLED_PhysicsGenerate, StepClimb) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	// Place 1-block wall at (258,256,61)
	hw.GetWorld().GetMap()->Set(258, 256, 61, true, 0x64ffffff, true);
	auto* p = SpawnPlayer(hw, {256.0F, 256.0F, 60.5F}, {0.3F, 0.0F, 0.0F},
	                      {1.0F, 0.0F, 0.0F});
	ASSERT_NE(p, nullptr);

	nlohmann::json ticks = nlohmann::json::array();
	for (int i = 0; i < 5; i++) {
		PlayerInput inp;
		inp.moveForward = true;
		p->SetInput(inp);
		hw.Advance(1);
		auto opt = hw.GetWorld().GetPlayer(0);
		ASSERT_NE(opt, nullptr);
		ticks.push_back(SnapshotPlayerTick(*opt, i));
	}
	WriteFixture("phys_step_climb_001.json",
	             BuildFixtureEnvelope("phys_step_climb_001", ticks));
}

// Step-climb suppression: same wall, crouch=true — climb suppressed, velocity.x zeroed.
TEST(DISABLED_PhysicsGenerate, StepClimbSuppression) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	// Place 1-block wall at (258,256,61)
	hw.GetWorld().GetMap()->Set(258, 256, 61, true, 0x64ffffff, true);
	auto* p = SpawnPlayer(hw, {256.0F, 256.0F, 60.5F}, {0.3F, 0.0F, 0.0F},
	                      {1.0F, 0.0F, 0.0F});
	ASSERT_NE(p, nullptr);

	nlohmann::json ticks = nlohmann::json::array();
	for (int i = 0; i < 5; i++) {
		PlayerInput inp;
		inp.moveForward = true;
		inp.crouch = true;
		p->SetInput(inp);
		hw.Advance(1);
		auto opt = hw.GetWorld().GetPlayer(0);
		ASSERT_NE(opt, nullptr);
		ticks.push_back(SnapshotPlayerTick(*opt, i));
	}
	WriteFixture("phys_step_climb_suppression_001.json",
	             BuildFixtureEnvelope("phys_step_climb_suppression_001", ticks));
}

// ===========================================================================
// Replay tests — load frozen fixture, re-run identical scenario, compare.
// ===========================================================================

TEST_F(PhysicsTestBase, GroundFriction) {
	auto fixture = LoadFixtureJson("phys_friction_ground_001.json");
	const auto& want_ticks = fixture.at("expected").at("ticks");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	SpawnPlayer(hw, {256.0F, 256.0F, 60.5F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
	auto got_ticks = RunAndCaptureTicks(hw, 10);

	ASSERT_EQ(want_ticks.size(), got_ticks.size());
	for (size_t i = 0; i < want_ticks.size(); i++) {
		ExpectSnapshotMatches(want_ticks[i], got_ticks[i],
		                      "expected.ticks[" + std::to_string(i) + "]");
	}
}

TEST_F(PhysicsTestBase, AirFriction) {
	auto fixture = LoadFixtureJson("phys_friction_air_001.json");
	const auto& want_ticks = fixture.at("expected").at("ticks");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	SpawnPlayer(hw, {256.0F, 256.0F, 32.0F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
	auto got_ticks = RunAndCaptureTicks(hw, 5);

	ASSERT_EQ(want_ticks.size(), got_ticks.size());
	for (size_t i = 0; i < want_ticks.size(); i++) {
		ExpectSnapshotMatches(want_ticks[i], got_ticks[i],
		                      "expected.ticks[" + std::to_string(i) + "]");
	}
}

TEST_F(PhysicsTestBase, WadeFriction) {
	auto fixture = LoadFixtureJson("phys_friction_wade_001.json");
	const auto& want_ticks = fixture.at("expected").at("ticks");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	SpawnPlayer(hw, {256.0F, 256.0F, 62.5F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
	auto got_ticks = RunAndCaptureTicks(hw, 10);

	ASSERT_EQ(want_ticks.size(), got_ticks.size());
	for (size_t i = 0; i < want_ticks.size(); i++) {
		ExpectSnapshotMatches(want_ticks[i], got_ticks[i],
		                      "expected.ticks[" + std::to_string(i) + "]");
	}
}

TEST_F(PhysicsTestBase, Jump) {
	auto fixture = LoadFixtureJson("phys_jump_001.json");
	const auto& want_ticks = fixture.at("expected").at("ticks");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto* p =
	  SpawnPlayer(hw, {256.0F, 256.0F, 60.5F}, {0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
	ASSERT_NE(p, nullptr);

	nlohmann::json got_ticks = nlohmann::json::array();
	for (int i = 0; i < 3; i++) {
		PlayerInput inp;
		inp.jump = (i == 0);
		p->SetInput(inp);
		hw.Advance(1);
		auto opt = hw.GetWorld().GetPlayer(0);
		ASSERT_NE(opt, nullptr);
		got_ticks.push_back(SnapshotPlayerTick(*opt, i));
	}

	ASSERT_EQ(want_ticks.size(), got_ticks.size());
	for (size_t i = 0; i < want_ticks.size(); i++) {
		ExpectSnapshotMatches(want_ticks[i], got_ticks[i],
		                      "expected.ticks[" + std::to_string(i) + "]");
	}
}

TEST_F(PhysicsTestBase, JumpCooldown) {
	auto fixture = LoadFixtureJson("phys_jump_cooldown_001.json");
	const auto& want_ticks = fixture.at("expected").at("ticks");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto* p =
	  SpawnPlayer(hw, {256.0F, 256.0F, 60.5F}, {0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
	ASSERT_NE(p, nullptr);

	bool jumpSeq[3] = {true, false, true};
	nlohmann::json got_ticks = nlohmann::json::array();
	for (int i = 0; i < 3; i++) {
		PlayerInput inp;
		inp.jump = jumpSeq[i];
		p->SetInput(inp);
		hw.Advance(1);
		auto opt = hw.GetWorld().GetPlayer(0);
		ASSERT_NE(opt, nullptr);
		got_ticks.push_back(SnapshotPlayerTick(*opt, i));
	}

	ASSERT_EQ(want_ticks.size(), got_ticks.size());
	for (size_t i = 0; i < want_ticks.size(); i++) {
		ExpectSnapshotMatches(want_ticks[i], got_ticks[i],
		                      "expected.ticks[" + std::to_string(i) + "]");
	}
}

TEST_F(PhysicsTestBase, CrouchOffset) {
	auto fixture = LoadFixtureJson("phys_crouch_offset_001.json");
	const auto& want_ticks = fixture.at("expected").at("ticks");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto* p =
	  SpawnPlayer(hw, {256.0F, 256.0F, 60.5F}, {0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
	ASSERT_NE(p, nullptr);

	nlohmann::json got_ticks = nlohmann::json::array();
	for (int i = 0; i < 3; i++) {
		PlayerInput inp;
		inp.crouch = true;
		p->SetInput(inp);
		hw.Advance(1);
		auto opt = hw.GetWorld().GetPlayer(0);
		ASSERT_NE(opt, nullptr);
		got_ticks.push_back(SnapshotPlayerTick(*opt, i));
	}

	ASSERT_EQ(want_ticks.size(), got_ticks.size());
	for (size_t i = 0; i < want_ticks.size(); i++) {
		ExpectSnapshotMatches(want_ticks[i], got_ticks[i],
		                      "expected.ticks[" + std::to_string(i) + "]");
	}
}

TEST_F(PhysicsTestBase, StepClimb) {
	auto fixture = LoadFixtureJson("phys_step_climb_001.json");
	const auto& want_ticks = fixture.at("expected").at("ticks");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	hw.GetWorld().GetMap()->Set(258, 256, 61, true, 0x64ffffff, true);
	auto* p =
	  SpawnPlayer(hw, {256.0F, 256.0F, 60.5F}, {0.3F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
	ASSERT_NE(p, nullptr);

	nlohmann::json got_ticks = nlohmann::json::array();
	for (int i = 0; i < 5; i++) {
		PlayerInput inp;
		inp.moveForward = true;
		p->SetInput(inp);
		hw.Advance(1);
		auto opt = hw.GetWorld().GetPlayer(0);
		ASSERT_NE(opt, nullptr);
		got_ticks.push_back(SnapshotPlayerTick(*opt, i));
	}

	ASSERT_EQ(want_ticks.size(), got_ticks.size());
	for (size_t i = 0; i < want_ticks.size(); i++) {
		ExpectSnapshotMatches(want_ticks[i], got_ticks[i],
		                      "expected.ticks[" + std::to_string(i) + "]");
	}
}

TEST_F(PhysicsTestBase, StepClimbSuppression) {
	auto fixture = LoadFixtureJson("phys_step_climb_suppression_001.json");
	const auto& want_ticks = fixture.at("expected").at("ticks");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	hw.GetWorld().GetMap()->Set(258, 256, 61, true, 0x64ffffff, true);
	auto* p =
	  SpawnPlayer(hw, {256.0F, 256.0F, 60.5F}, {0.3F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F});
	ASSERT_NE(p, nullptr);

	nlohmann::json got_ticks = nlohmann::json::array();
	for (int i = 0; i < 5; i++) {
		PlayerInput inp;
		inp.moveForward = true;
		inp.crouch = true;
		p->SetInput(inp);
		hw.Advance(1);
		auto opt = hw.GetWorld().GetPlayer(0);
		ASSERT_NE(opt, nullptr);
		got_ticks.push_back(SnapshotPlayerTick(*opt, i));
	}

	ASSERT_EQ(want_ticks.size(), got_ticks.size());
	for (size_t i = 0; i < want_ticks.size(); i++) {
		ExpectSnapshotMatches(want_ticks[i], got_ticks[i],
		                      "expected.ticks[" + std::to_string(i) + "]");
	}
}
