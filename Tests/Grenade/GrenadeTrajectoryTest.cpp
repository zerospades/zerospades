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

// WEAP-03: deterministic N-tick grenade trajectory goldens — gravity + floor (z)
// bounce + wall (x/y) bounce, water landing at integer z>=63, fuse countdown ->
// Explode. All three fixtures are kind "step_trace" with the per-tick grenade
// series under expected.ticks[] (the frozen fixture_schema.json reserves
// world_snapshot for tick/players; grenade traces use step_trace, mirroring
// Tests/Physics/PhysicsFixtureTest.cpp).
//
// Pattern: generate-then-freeze.
//   - DISABLED_GrenadeTrajectoryGenerate.* tests emit expected.ticks from the
//     C++ oracle (Grenade::Update + MoveGrenade). Run manually once, commit the
//     frozen fixture, then never again.
//   - GrenadeTrajectoryTest.* replay tests load the frozen fixture, re-run the
//     identical scenario in-process, and assert match via ExpectSnapshotMatches
//     (GRENADE_TOL = 1e-3 — never EXPECT_FLOAT_EQ).
//
// Grenade physics (Grenade.cpp:42-122, VERIFIED):
//   Update(dt): fuse -= dt; if (fuse < 0) { Explode(); return true; }  // MoveGrenade SKIPPED
//               else MoveGrenade(dt); ret==2 -> GrenadeBounced, ret==-1 -> GrenadeDroppedIntoWater.
//   MoveGrenade: f = dt*32; velocity.z += dt; position += velocity*f. lp=pos.Floor(),
//                lp2=oldPos.Floor(). ret=-1 if (lp.z>=63 && lp2.z<63). If ClipWorld(lp) solid:
//                ret=1/2, reflect one axis (z, then x, then y), position=oldPos, velocity*=0.36.
//   CRITICAL (Pitfall 6 / A2): ClipWorld overwrites ret to 1/2 if the landing cell is solid.
//   The flat map is solid ONLY at z=62. So the water fixture carves the landing column
//   (Set(x,y,62,false,...)) so z>=63 is reachable as air and ret==-1 survives.

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <Client/GameConstants.h>
#include <Client/GameMap.h>
#include <Client/Grenade.h>
#include <Client/World.h>

#include "GrenadeSnapshot.h"
#include "HeadlessWorld.h"
#include "MakeFlatMap.h"
#include "SettingsGuard.h"
#include "ToleranceMatchers.h"

using namespace spades;
using namespace spades::tests;
using namespace spades::client;

namespace {

	// ---------------------------------------------------------------------------
	// LoadFixtureJson / ExpectSnapshotMatches: self-contained copies. Tests/Physics
	// is not on the Tests/Grenade include path (only Tests/Helpers + Sources are —
	// Tests/CMakeLists.txt target_include_directories), so PhysicsTestBase.h is
	// unreachable. Behaviour identical to PhysicsTestBase.h:45-95 / the inlined copy
	// in Tests/Weapons/WeaponTestBase.h (07-02 Rule-3 precedent).
	// ---------------------------------------------------------------------------
	nlohmann::json LoadGrenadeFixtureJson(const std::string& name) {
		std::string path = std::string(TESTS_DIR) + "/../fixtures/" + name;
		std::ifstream f(path);
		if (!f.is_open())
			SPRaise("Cannot open fixture: %s", path.c_str());
		nlohmann::json j;
		f >> j;
		return j;
	}

	void ExpectSnapshotMatches(const nlohmann::json& want, const nlohmann::json& got,
	                           const std::string& path) {
		if (want.is_object()) {
			ASSERT_TRUE(got.is_object()) << "at " << path << ": expected object";
			for (auto& [key, wv] : want.items()) {
				ASSERT_TRUE(got.contains(key)) << "at " << path << ": missing key '" << key << "'";
				ExpectSnapshotMatches(wv, got.at(key), path + "." + key);
			}
			for (auto& [key, gv] : got.items()) {
				(void)gv;
				EXPECT_TRUE(want.contains(key))
				  << "at " << path << ": unexpected extra key '" << key << "'";
			}
			return;
		}
		if (want.is_array()) {
			ASSERT_TRUE(got.is_array()) << "at " << path << ": expected array";
			ASSERT_EQ(want.size(), got.size()) << "at " << path << ": array size mismatch";
			for (size_t i = 0; i < want.size(); i++)
				ExpectSnapshotMatches(want[i], got[i], path + "[" + std::to_string(i) + "]");
			return;
		}
		if (want.is_number_float() || got.is_number_float()) {
			ASSERT_TRUE(want.is_number() && got.is_number())
			  << "at " << path << ": expected numeric";
			EXPECT_NEAR(want.get<double>(), got.get<double>(), ToleranceForField(path))
			  << "at " << path;
			return;
		}
		EXPECT_EQ(want, got) << "at " << path;
	}

	void WriteFixture(const std::string& name, const nlohmann::json& j) {
		std::string path = std::string(TESTS_DIR) + "/../fixtures/" + name;
		std::ofstream f(path);
		ASSERT_TRUE(f.is_open()) << "Cannot open for write: " << path;
		f << j.dump(4) << "\n";
	}

	// ---------------------------------------------------------------------------
	// BuildGrenadeFixtureEnvelope: assemble the step_trace fixture envelope.
	// subsystem = "weap" (there is NO grenade subsystem — grenade fixtures use the
	// weap_ prefix; validate_fixtures.py enforces it). behavior implementation_detail.
	// ---------------------------------------------------------------------------
	nlohmann::json BuildGrenadeFixtureEnvelope(const std::string& id, const nlohmann::json& ticks) {
		nlohmann::json j;
		j["version"] = "1.0.0";
		j["id"] = id;
		j["subsystem"] = "weap";
		j["behavior"] = "implementation_detail";
		j["seed"] = 42;
		j["protocol_version"] = "0.75";
		j["map"] = {{"generator", "flat"}, {"ground_z", 62}};
		j["inputs"] = nlohmann::json::array();
		j["kind"] = "step_trace";
		j["expected"] = {{"ticks", ticks}};
		return j;
	}

	// One tick object: GrenadeSnapshot {position,velocity,fuse_s} + tick index + exploded.
	// The step_trace schema only requires ticks to be an array (no per-item shape lock),
	// so the extra tick/exploded fields are accepted. tick/exploded compare exactly;
	// position/velocity/fuse_s map to GRENADE_TOL via ToleranceForField (the "grenade"
	// path token below selects 1e-3).
	nlohmann::json GrenadeTickObj(const client::Grenade& g, int tick, bool exploded) {
		nlohmann::json t = SnapshotGrenadeTick(g);
		t["tick"] = tick;
		t["exploded"] = exploded;
		return t;
	}

} // namespace

// ===========================================================================
// DISABLED_ generator tests — run manually once, then commit the frozen fixture.
// ===========================================================================

// (a) Bounce trajectory — gravity + floor (z) bounce + wall (x/y) bounce.
// Grenade dropped above the z=62 flat-map floor with horizontal velocity toward
// a 2-block wall at x=258 (z=60,z=61). Gravity (+z) pulls it down onto the floor
// (z bounce, velocity.z reflected + *0.36); horizontal motion carries it into the
// wall (x bounce, velocity.x reflected + *0.36). Fuse high (5.0) so it never
// explodes during the trajectory. Every tick captured (Nyquist; the 0.36
// reflection is emergent across the pre/post-bounce ticks).
TEST(DISABLED_GrenadeTrajectoryGenerate, Bounce) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	// 2-block wall at x=258 so the grenade bounces off it horizontally.
	hw.GetWorld().GetMap()->Set(258, 256, 61, true, 0x64ffffff, /*unsafe=*/true);
	hw.GetWorld().GetMap()->Set(258, 256, 60, true, 0x64ffffff, /*unsafe=*/true);

	auto grenade = std::make_unique<Grenade>(hw.GetWorld(), Vector3{256.5F, 256.5F, 60.2F},
	                                         Vector3{0.6F, 0.0F, 0.2F}, 5.0F);
	Grenade* g = grenade.get();
	hw.GetWorld().AddGrenade(std::move(grenade));

	nlohmann::json ticks = nlohmann::json::array();
	for (int i = 0; i < 40; i++) {
		bool exploded = g->Update(FIXED_DT);
		ticks.push_back(GrenadeTickObj(*g, i, exploded));
		if (exploded)
			break;
	}
	WriteFixture("weap_step_trace_grenade_bounce.json",
	             BuildGrenadeFixtureEnvelope("weap_step_trace_grenade_bounce", ticks));
}

// (b) Water landing — grenade crosses integer z>=63 over a NON-solid (carved)
// column so MoveGrenade returns -1 (GrenadeDroppedIntoWater), not a wall bounce.
// The flat map is solid only at z=62; carving (256,256,62) non-solid makes the
// whole column at x=256,y=256 air, so a grenade starting just above z=63 with
// downward (+z) velocity crosses lp2.z<63 -> lp.z>=63 in open space.
TEST(DISABLED_GrenadeTrajectoryGenerate, Water) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto map = hw.GetWorld().GetMap();
	// Carve the landing column so z=63 is reachable as air (Pitfall 6 / A2).
	map->Set(256, 256, 62, false, 0, /*unsafe=*/true);
	// Sanity: the landing cell (256,256,63) must be non-solid BEFORE freezing,
	// else MoveGrenade overwrites ret to a wall hit and GrenadeDroppedIntoWater
	// never fires.
	ASSERT_FALSE(map->ClipWorld(256, 256, 63))
	  << "landing cell (256,256,63) is solid — water crossing would be a wall hit";
	ASSERT_FALSE(map->ClipWorld(256, 256, 62))
	  << "carved cell (256,256,62) is still solid — carve failed";

	// Start just above the z=63 plane with downward (+z) velocity.
	auto grenade = std::make_unique<Grenade>(hw.GetWorld(), Vector3{256.5F, 256.5F, 62.5F},
	                                         Vector3{0.0F, 0.0F, 0.6F}, 5.0F);
	Grenade* g = grenade.get();
	hw.GetWorld().AddGrenade(std::move(grenade));

	int waterBefore = hw.GetListener().grenadeWaterCount;
	nlohmann::json ticks = nlohmann::json::array();
	for (int i = 0; i < 20; i++) {
		bool exploded = g->Update(FIXED_DT);
		ticks.push_back(GrenadeTickObj(*g, i, exploded));
		if (exploded)
			break;
		if (hw.GetListener().grenadeWaterCount > waterBefore)
			break; // captured the crossing tick; trajectory below is undefined (no floor)
	}
	ASSERT_GT(hw.GetListener().grenadeWaterCount, waterBefore)
	  << "grenade never crossed z>=63 into water";
	WriteFixture("weap_step_trace_grenade_water.json",
	             BuildGrenadeFixtureEnvelope("weap_step_trace_grenade_water", ticks));
}

// (c) Fuse countdown -> Explode. Short fuse (0.1s ~= 6 ticks) and small velocity on
// the flat map, starting well above the z=62 floor so the grenade never bounces
// before the fuse expires. Per Pitfall 7: on the explode tick Update decrements
// fuse below 0, calls Explode(), and returns true WITHOUT running MoveGrenade — so
// the explode tick's position == the prior tick's position (no further motion) and
// fuse_s has just gone negative. The loop captures up to AND INCLUDING that tick.
TEST(DISABLED_GrenadeTrajectoryGenerate, Fuse) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	// Start at z=40 (well above the z=62 floor — falls but never reaches the floor
	// within the ~6-tick fuse window) with a small horizontal velocity.
	auto grenade = std::make_unique<Grenade>(hw.GetWorld(), Vector3{256.5F, 256.5F, 40.0F},
	                                         Vector3{0.1F, 0.0F, 0.0F}, 0.1F);
	Grenade* g = grenade.get();
	hw.GetWorld().AddGrenade(std::move(grenade));

	nlohmann::json ticks = nlohmann::json::array();
	for (int i = 0; i < 20; i++) {
		bool exploded = g->Update(FIXED_DT);
		ticks.push_back(GrenadeTickObj(*g, i, exploded));
		if (exploded)
			break;
	}
	// Exactly one Explode/GrenadeExploded must have fired by now.
	ASSERT_EQ(hw.GetListener().grenadeExplodedCount, 1)
	  << "fuse never expired (or exploded more than once)";
	WriteFixture("weap_step_trace_grenade_fuse.json",
	             BuildGrenadeFixtureEnvelope("weap_step_trace_grenade_fuse", ticks));
}

// ===========================================================================
// Replay tests — load the frozen fixture, re-run, compare at GRENADE_TOL.
// ===========================================================================

TEST(GrenadeTrajectoryTest, Bounce) {
	SettingsGuard guard;
	auto fixture = LoadGrenadeFixtureJson("weap_step_trace_grenade_bounce.json");
	const auto& want_ticks = fixture.at("expected").at("ticks");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	hw.GetWorld().GetMap()->Set(258, 256, 61, true, 0x64ffffff, true);
	hw.GetWorld().GetMap()->Set(258, 256, 60, true, 0x64ffffff, true);

	auto grenade = std::make_unique<Grenade>(hw.GetWorld(), Vector3{256.5F, 256.5F, 60.2F},
	                                         Vector3{0.6F, 0.0F, 0.2F}, 5.0F);
	Grenade* g = grenade.get();
	hw.GetWorld().AddGrenade(std::move(grenade));

	nlohmann::json got_ticks = nlohmann::json::array();
	for (int i = 0; i < 40; i++) {
		bool exploded = g->Update(FIXED_DT);
		got_ticks.push_back(GrenadeTickObj(*g, i, exploded));
		if (exploded)
			break;
	}

	ASSERT_EQ(want_ticks.size(), got_ticks.size());
	for (size_t i = 0; i < want_ticks.size(); i++)
		ExpectSnapshotMatches(want_ticks[i], got_ticks[i],
		                      "grenade.ticks[" + std::to_string(i) + "]");
	// The trajectory bounces off the floor and the wall — assert both fired.
	EXPECT_GT(hw.GetListener().grenadeBouncedCount, 0);
}

TEST(GrenadeTrajectoryTest, Water) {
	SettingsGuard guard;
	auto fixture = LoadGrenadeFixtureJson("weap_step_trace_grenade_water.json");
	const auto& want_ticks = fixture.at("expected").at("ticks");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto map = hw.GetWorld().GetMap();
	map->Set(256, 256, 62, false, 0, true);
	ASSERT_FALSE(map->ClipWorld(256, 256, 63));

	auto grenade = std::make_unique<Grenade>(hw.GetWorld(), Vector3{256.5F, 256.5F, 62.5F},
	                                         Vector3{0.0F, 0.0F, 0.6F}, 5.0F);
	Grenade* g = grenade.get();
	hw.GetWorld().AddGrenade(std::move(grenade));

	int waterBefore = hw.GetListener().grenadeWaterCount;
	nlohmann::json got_ticks = nlohmann::json::array();
	for (int i = 0; i < 20; i++) {
		bool exploded = g->Update(FIXED_DT);
		got_ticks.push_back(GrenadeTickObj(*g, i, exploded));
		if (exploded)
			break;
		if (hw.GetListener().grenadeWaterCount > waterBefore)
			break;
	}

	ASSERT_EQ(want_ticks.size(), got_ticks.size());
	for (size_t i = 0; i < want_ticks.size(); i++)
		ExpectSnapshotMatches(want_ticks[i], got_ticks[i],
		                      "grenade.ticks[" + std::to_string(i) + "]");
	EXPECT_GE(hw.GetListener().grenadeWaterCount, 1);
}

TEST(GrenadeTrajectoryTest, Fuse) {
	SettingsGuard guard;
	auto fixture = LoadGrenadeFixtureJson("weap_step_trace_grenade_fuse.json");
	const auto& want_ticks = fixture.at("expected").at("ticks");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto grenade = std::make_unique<Grenade>(hw.GetWorld(), Vector3{256.5F, 256.5F, 40.0F},
	                                         Vector3{0.1F, 0.0F, 0.0F}, 0.1F);
	Grenade* g = grenade.get();
	hw.GetWorld().AddGrenade(std::move(grenade));

	nlohmann::json got_ticks = nlohmann::json::array();
	for (int i = 0; i < 20; i++) {
		bool exploded = g->Update(FIXED_DT);
		got_ticks.push_back(GrenadeTickObj(*g, i, exploded));
		if (exploded)
			break;
	}

	ASSERT_EQ(want_ticks.size(), got_ticks.size());
	for (size_t i = 0; i < want_ticks.size(); i++)
		ExpectSnapshotMatches(want_ticks[i], got_ticks[i],
		                      "grenade.ticks[" + std::to_string(i) + "]");
	// Exactly one Explode/GrenadeExploded fired (Pitfall 7).
	EXPECT_EQ(hw.GetListener().grenadeExplodedCount, 1);
	// The explode tick (last) reuses the prior tick's position — MoveGrenade skipped.
	ASSERT_GE(got_ticks.size(), 2u);
	const auto& last = got_ticks[got_ticks.size() - 1];
	const auto& prev = got_ticks[got_ticks.size() - 2];
	EXPECT_TRUE(last.at("exploded").get<bool>());
	EXPECT_FALSE(prev.at("exploded").get<bool>());
	EXPECT_NEAR(last["position"]["x"].get<double>(), prev["position"]["x"].get<double>(),
	            GRENADE_TOL);
	EXPECT_NEAR(last["position"]["y"].get<double>(), prev["position"]["y"].get<double>(),
	            GRENADE_TOL);
	EXPECT_NEAR(last["position"]["z"].get<double>(), prev["position"]["z"].get<double>(),
	            GRENADE_TOL);
	EXPECT_LT(last["fuse_s"].get<double>(), 0.0); // fuse went negative on explode
}
