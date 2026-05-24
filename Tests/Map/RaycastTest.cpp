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

// MAP-02 raycast/collision fixture tests.
//
// Pattern: generate-then-freeze (Phase 5 established).
//   - DISABLED_RaycastGenerate.* emit expected values from the C++ oracle.
//     Run manually once with --gtest_also_run_disabled_tests, commit the frozen
//     fixtures, then never again.
//   - RaycastFixture_* replay tests load the frozen fixture, re-run the identical
//     scenario in-process, and assert match via EXPECT_NEAR (RAYCAST_TOL) for
//     float vectors and EXPECT_EQ (exact) for bool/int fields.
//
// Families covered (MAP-02):
//   CastRay2 hit     — ray fires toward a solid block; validates hitPos/hitBlock/normal
//   CastRay2 miss    — ray fires but no block in path; validates hit=false
//   CastRay2 startSolid — ray origin inside solid; validates hit=true, startSolid=true
//   ClipBox          — z-boundary semantics: z==63→62 remap; z<0 not clipped; z>=64 clipped
//   ClipWorld        — z-boundary semantics: z<0 not clipped; z>=64 clipped (same remap)
//                      XY out-of-bounds differs: ClipBox → true, ClipWorld → false
//
// stepCount: RayCastResult has no stepCount field; we track it by counting loop
// iterations separately using the same algorithm so the fixture captures a
// consistent value from the oracle run.
//
// Per 06-PATTERNS.md Tolerance Matching:
//   RAYCAST_TOL (1e-6) for hitPos vectors (float values).
//   Exact (EXPECT_EQ) for hit/startSolid bools, hitBlock/normal int coords.

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <Client/GameMap.h>
#include <Client/World.h>
#include <Core/Math.h>

#include "HeadlessWorld.h"
#include "MapTestBase.h"
#include "MakeFlatMap.h"
#include "SettingsGuard.h"
#include "ToleranceMatchers.h"

using namespace spades;
using namespace spades::tests;
using namespace spades::client;

namespace {

	// -------------------------------------------------------------------------
	// BuildFixtureEnvelope: assemble the full fixture JSON envelope.
	// Mirrors BlockStateTest.cpp — subsystem="map", kind="value_lookup".
	// -------------------------------------------------------------------------
	nlohmann::json BuildFixtureEnvelope(const std::string& id) {
		nlohmann::json j;
		j["version"] = "1.0.0";
		j["id"] = id;
		j["subsystem"] = "map";
		j["behavior"] = "implementation_detail";
		j["seed"] = 42;
		j["protocol_version"] = "0.75";
		j["map"] = {{"generator", "flat"}, {"ground_z", 62}};
		j["inputs"] = nlohmann::json::array();
		j["kind"] = "value_lookup";
		j["expected"] = {{"value", nlohmann::json::object()}};
		return j;
	}

	// -------------------------------------------------------------------------
	// WriteFixture: serialize fixture JSON to fixtures/ directory.
	// Path resolved via TESTS_DIR compile-time constant (Tests/CMakeLists.txt).
	// -------------------------------------------------------------------------
	void WriteFixture(const std::string& name, const nlohmann::json& j) {
		std::string path = std::string(TESTS_DIR) + "/../fixtures/" + name;
		std::ofstream f(path);
		ASSERT_TRUE(f.is_open()) << "Cannot open for write: " << path;
		f << j.dump(4) << "\n";
	}

	// -------------------------------------------------------------------------
	// CountRaySteps: count how many DDA steps CastRay2 takes before hitting.
	// Mirrors GameMap::CastRay2 loop logic to produce a step count for fixtures.
	// Returns the 0-based step index where the hit was found, or -1 for miss.
	// startSolid path (origin inside solid) returns stepCount = 0.
	// -------------------------------------------------------------------------
	int CountRaySteps(const GameMap& map, Vector3 v0, Vector3 dir, int maxSteps) {
		dir = dir.Normalize();
		IntVector3 iv = v0.Floor();

		// startSolid: origin inside solid → 0 steps
		if (map.IsSolidWrapped(iv.x, iv.y, iv.z))
			return 0;

		Vector3 fv;
		fv.x = (dir.x > 0.0F) ? (float)(iv.x + 1) - v0.x : v0.x - (float)iv.x;
		fv.y = (dir.y > 0.0F) ? (float)(iv.y + 1) - v0.y : v0.y - (float)iv.y;
		fv.z = (dir.z > 0.0F) ? (float)(iv.z + 1) - v0.z : v0.z - (float)iv.z;

		float invX = (dir.x != 0.0F) ? 1.0F / fabsf(dir.x) : dir.x;
		float invY = (dir.y != 0.0F) ? 1.0F / fabsf(dir.y) : dir.y;
		float invZ = (dir.z != 0.0F) ? 1.0F / fabsf(dir.z) : dir.z;

		for (int i = 0; i < maxSteps; i++) {
			IntVector3 nextBlock;
			int hasNextBlock = 0;
			float nextBlockTime = 0.0F;

			if (invX != 0.0F) {
				nextBlock = iv;
				if (dir.x > 0.0F) nextBlock.x++; else nextBlock.x--;
				nextBlockTime = fv.x * invX;
				hasNextBlock = 1;
			}
			if (invY != 0.0F) {
				float t = fv.y * invY;
				if (!hasNextBlock || t < nextBlockTime) {
					nextBlock = iv;
					if (dir.y > 0.0F) nextBlock.y++; else nextBlock.y--;
					nextBlockTime = t;
					hasNextBlock = 2;
				}
			}
			if (invZ != 0.0F) {
				float t = fv.z * invZ;
				if (!hasNextBlock || t < nextBlockTime) {
					nextBlock = iv;
					if (dir.z > 0.0F) nextBlock.z++; else nextBlock.z--;
					nextBlockTime = t;
					hasNextBlock = 3;
				}
			}

			fv.x = (hasNextBlock == 1) ? 1.0F : fv.x - fabsf(dir.x) * nextBlockTime;
			fv.y = (hasNextBlock == 2) ? 1.0F : fv.y - fabsf(dir.y) * nextBlockTime;
			fv.z = (hasNextBlock == 3) ? 1.0F : fv.z - fabsf(dir.z) * nextBlockTime;

			if (map.IsSolidWrapped(nextBlock.x, nextBlock.y, nextBlock.z))
				return i + 1; // 1-based step count (first step = 1)

			iv = nextBlock;
		}

		return -1; // miss
	}

} // namespace

// ===========================================================================
// DISABLED_ generator tests — run once manually, commit frozen fixtures.
// ===========================================================================

// CastRay2 hit: place a solid block at (200, 200, 50).
// Fire ray from (198.5, 200.5, 50.5) toward +x direction.
// The ray travels entirely in the x-axis, hits block at (200, 200, 50).
// Expected: hit=true, startSolid=false, hitBlock=(200,200,50), normal=(-1,0,0).
TEST(DISABLED_RaycastGenerate, Hit) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// Place a solid block at a known position
	map.Set(200, 200, 50, true, 0xFFFFFFFF, true);

	// Ray origin in the same y/z plane, pointing +x toward the block
	Vector3 v0(198.5F, 200.5F, 50.5F);
	Vector3 dir(1.0F, 0.0F, 0.0F); // normalized

	auto result = map.CastRay2(v0, dir, 100);
	int stepCount = CountRaySteps(map, v0, dir, 100);

	auto j = BuildFixtureEnvelope("map_value_lookup_006_raycast_hit");
	j["expected"]["value"] = {
	    {"hit", result.hit},
	    {"startSolid", result.startSolid},
	    // hitPos: float values stored as double for lossless JSON round-trip
	    {"hitPos", {static_cast<double>(result.hitPos.x),
	                static_cast<double>(result.hitPos.y),
	                static_cast<double>(result.hitPos.z)}},
	    // hitBlock: integer block coordinate
	    {"hitBlock", {result.hitBlock.x, result.hitBlock.y, result.hitBlock.z}},
	    // normal: IntVector3 (±1 or 0 per face)
	    {"normal", {result.normal.x, result.normal.y, result.normal.z}},
	    {"stepCount", stepCount},
	    // Store ray setup for replay reconstruction
	    {"ray_ox", static_cast<double>(v0.x)},
	    {"ray_oy", static_cast<double>(v0.y)},
	    {"ray_oz", static_cast<double>(v0.z)},
	    {"ray_dx", static_cast<double>(dir.x)},
	    {"ray_dy", static_cast<double>(dir.y)},
	    {"ray_dz", static_cast<double>(dir.z)},
	    {"block_x", 200},
	    {"block_y", 200},
	    {"block_z", 50},
	};
	WriteFixture("map_value_lookup_006_raycast_hit.json", j);
}

// CastRay2 miss: fire ray from (100.5, 100.5, 30.5) toward -z (upward).
// No solid blocks in the path (flat map has ground at z=62, not above z=30).
// Expected: hit=false.
TEST(DISABLED_RaycastGenerate, Miss) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// Fire upward from open air — no solid blocks between z=30 and z=0
	Vector3 v0(100.5F, 100.5F, 30.5F);
	Vector3 dir(0.0F, 0.0F, -1.0F); // normalized, pointing up (z decreases)

	auto result = map.CastRay2(v0, dir, 50); // 50 steps: z=30 to z=30-50 = never hits

	auto j = BuildFixtureEnvelope("map_value_lookup_007_raycast_miss");
	j["expected"]["value"] = {
	    {"hit", result.hit},
	    {"startSolid", result.startSolid},
	    {"ray_ox", static_cast<double>(v0.x)},
	    {"ray_oy", static_cast<double>(v0.y)},
	    {"ray_oz", static_cast<double>(v0.z)},
	    {"ray_dx", static_cast<double>(dir.x)},
	    {"ray_dy", static_cast<double>(dir.y)},
	    {"ray_dz", static_cast<double>(dir.z)},
	    {"maxSteps", 50},
	};
	WriteFixture("map_value_lookup_007_raycast_miss.json", j);
}

// CastRay2 startSolid: ray origin INSIDE a solid block at (50, 50, 50).
// Ray fires from (50.5, 50.5, 50.5) outward — any direction.
// Expected: hit=true, startSolid=true, hitPos≈origin, stepCount=0 (immediate).
TEST(DISABLED_RaycastGenerate, StartSolid) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// Place solid block at (50, 50, 50)
	map.Set(50, 50, 50, true, 0xFFFFFFFF, true);

	// Ray origin inside the solid block
	Vector3 v0(50.5F, 50.5F, 50.5F);
	Vector3 dir(1.0F, 0.0F, 0.0F); // direction doesn't matter for startSolid

	auto result = map.CastRay2(v0, dir, 100);
	int stepCount = CountRaySteps(map, v0, dir, 100);

	auto j = BuildFixtureEnvelope("map_value_lookup_008_raycast_startsolid");
	j["expected"]["value"] = {
	    {"hit", result.hit},
	    {"startSolid", result.startSolid},
	    {"hitPos", {static_cast<double>(result.hitPos.x),
	                static_cast<double>(result.hitPos.y),
	                static_cast<double>(result.hitPos.z)}},
	    {"hitBlock", {result.hitBlock.x, result.hitBlock.y, result.hitBlock.z}},
	    {"normal", {result.normal.x, result.normal.y, result.normal.z}},
	    {"stepCount", stepCount},
	    {"ray_ox", static_cast<double>(v0.x)},
	    {"ray_oy", static_cast<double>(v0.y)},
	    {"ray_oz", static_cast<double>(v0.z)},
	    {"ray_dx", static_cast<double>(dir.x)},
	    {"ray_dy", static_cast<double>(dir.y)},
	    {"ray_dz", static_cast<double>(dir.z)},
	    {"block_x", 50},
	    {"block_y", 50},
	    {"block_z", 50},
	};
	WriteFixture("map_value_lookup_008_raycast_startsolid.json", j);
}

// ClipBox z-boundary semantics (DefaultDepth = 64):
//   z < 0  → false (not clipped)
//   z == 62 (flat map ground) → IsSolid(x,y,62) = true → clipped
//   z == 63 → remaps to 62 → IsSolid(x,y,62) = true → clipped
//   z == 64 → directly clipped (>= DefaultDepth) → true
//   XY out-of-bounds (x=-1) → true (clipped)
//
// In-bounds XY (100, 100) used for z tests; x=-1 for XY out-of-bounds test.
TEST(DISABLED_RaycastGenerate, ClipBox) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// Use in-bounds coordinates for z-boundary tests.
	// Flat map: (100,100,62) is solid ground.
	bool zneg = map.ClipBox(100, 100, -1);    // z<0 → false
	bool z62  = map.ClipBox(100, 100, 62);    // solid ground → true
	bool z63  = map.ClipBox(100, 100, 63);    // remaps to 62 (solid) → true
	bool z64  = map.ClipBox(100, 100, 64);    // >= DefaultDepth → true
	bool xyoob = map.ClipBox(-1, 100, 30);    // x out-of-bounds → true
	// Air column test: z=30 in (100,100) is air
	bool z30  = map.ClipBox(100, 100, 30);    // air → false

	auto j = BuildFixtureEnvelope("map_value_lookup_009_clip_box");
	j["expected"]["value"] = {
	    {"z_neg1_clipped",   zneg},
	    {"z_30_clipped",     z30},
	    {"z_62_clipped",     z62},
	    {"z_63_clipped",     z63},
	    {"z_64_clipped",     z64},
	    {"xy_oob_clipped",   xyoob},
	    // Document the test coordinates for replay
	    {"x", 100}, {"y", 100},
	};
	WriteFixture("map_value_lookup_009_clip_box.json", j);
}

// ClipWorld z-boundary semantics (DefaultDepth = 64):
//   z < 0  → false (not clipped — XY is also not clipped when z<0)
//   z == 62 (flat map ground) → IsSolid(x,y,62) = true → clipped
//   z == 63 → remaps to 62 → IsSolid(x,y,62) = true → clipped
//   z == 64 → >= DefaultDepth-1 (and != 63) → true
//   XY out-of-bounds (x=-1) → false (NOT clipped — key difference from ClipBox)
//
// Key difference vs ClipBox: out-of-bounds XY returns false (not clipped).
TEST(DISABLED_RaycastGenerate, ClipWorld) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	bool zneg  = map.ClipWorld(100, 100, -1);   // z<0 → false
	bool z30   = map.ClipWorld(100, 100, 30);    // air → false
	bool z62   = map.ClipWorld(100, 100, 62);    // solid ground → true
	bool z63   = map.ClipWorld(100, 100, 63);    // remaps to 62 (solid) → true
	bool z64   = map.ClipWorld(100, 100, 64);    // >= DefaultDepth-1, !=63 → true
	bool xyoob = map.ClipWorld(-1, 100, 30);     // x out-of-bounds → false (differs from ClipBox)

	auto j = BuildFixtureEnvelope("map_value_lookup_010_clip_world");
	j["expected"]["value"] = {
	    {"z_neg1_clipped",   zneg},
	    {"z_30_clipped",     z30},
	    {"z_62_clipped",     z62},
	    {"z_63_clipped",     z63},
	    {"z_64_clipped",     z64},
	    {"xy_oob_clipped",   xyoob},
	    {"x", 100}, {"y", 100},
	};
	WriteFixture("map_value_lookup_010_clip_world.json", j);
}

// ===========================================================================
// Enabled replay tests — run on every build, load frozen fixtures, assert.
// ===========================================================================

// RaycastFixture_Hit: replay CastRay2 hit scenario.
// Exact comparison for hit/startSolid/hitBlock/normal (bool/int).
// RAYCAST_TOL comparison for hitPos (float vector).
TEST_F(MapTestBase, RaycastFixture_Hit) {
	auto j = LoadFixtureJson("map_value_lookup_006_raycast_hit.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// Reconstruct scenario: place block at stored coordinates
	int bx = val.at("block_x").get<int>();
	int by = val.at("block_y").get<int>();
	int bz = val.at("block_z").get<int>();
	map.Set(bx, by, bz, true, 0xFFFFFFFF, true);

	// Reconstruct ray from fixture
	Vector3 v0(static_cast<float>(val.at("ray_ox").get<double>()),
	           static_cast<float>(val.at("ray_oy").get<double>()),
	           static_cast<float>(val.at("ray_oz").get<double>()));
	Vector3 dir(static_cast<float>(val.at("ray_dx").get<double>()),
	            static_cast<float>(val.at("ray_dy").get<double>()),
	            static_cast<float>(val.at("ray_dz").get<double>()));

	auto result = map.CastRay2(v0, dir, 100);

	// Bool fields — exact
	EXPECT_EQ(result.hit, val.at("hit").get<bool>());
	EXPECT_EQ(result.startSolid, val.at("startSolid").get<bool>());

	// hitPos — RAYCAST_TOL tolerance for float vector
	const auto& hp = val.at("hitPos");
	EXPECT_NEAR(static_cast<double>(result.hitPos.x), hp.at(0).get<double>(), RAYCAST_TOL);
	EXPECT_NEAR(static_cast<double>(result.hitPos.y), hp.at(1).get<double>(), RAYCAST_TOL);
	EXPECT_NEAR(static_cast<double>(result.hitPos.z), hp.at(2).get<double>(), RAYCAST_TOL);

	// hitBlock — integer, exact
	const auto& hb = val.at("hitBlock");
	EXPECT_EQ(result.hitBlock.x, hb.at(0).get<int>());
	EXPECT_EQ(result.hitBlock.y, hb.at(1).get<int>());
	EXPECT_EQ(result.hitBlock.z, hb.at(2).get<int>());

	// normal — IntVector3, exact
	const auto& nm = val.at("normal");
	EXPECT_EQ(result.normal.x, nm.at(0).get<int>());
	EXPECT_EQ(result.normal.y, nm.at(1).get<int>());
	EXPECT_EQ(result.normal.z, nm.at(2).get<int>());

	// stepCount — exact
	int stepCount = CountRaySteps(map, v0, dir, 100);
	EXPECT_EQ(stepCount, val.at("stepCount").get<int>());
}

// RaycastFixture_Miss: replay CastRay2 miss scenario.
// Only hit=false is asserted (miss path returns no meaningful hitPos/hitBlock).
TEST_F(MapTestBase, RaycastFixture_Miss) {
	auto j = LoadFixtureJson("map_value_lookup_007_raycast_miss.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	Vector3 v0(static_cast<float>(val.at("ray_ox").get<double>()),
	           static_cast<float>(val.at("ray_oy").get<double>()),
	           static_cast<float>(val.at("ray_oz").get<double>()));
	Vector3 dir(static_cast<float>(val.at("ray_dx").get<double>()),
	            static_cast<float>(val.at("ray_dy").get<double>()),
	            static_cast<float>(val.at("ray_dz").get<double>()));
	int maxSteps = val.at("maxSteps").get<int>();

	auto result = map.CastRay2(v0, dir, maxSteps);

	EXPECT_EQ(result.hit, val.at("hit").get<bool>());
}

// RaycastFixture_StartSolid: replay CastRay2 startSolid scenario.
// Expected: hit=true, startSolid=true, hitPos≈origin, stepCount=0.
TEST_F(MapTestBase, RaycastFixture_StartSolid) {
	auto j = LoadFixtureJson("map_value_lookup_008_raycast_startsolid.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// Place block at stored coordinates
	int bx = val.at("block_x").get<int>();
	int by = val.at("block_y").get<int>();
	int bz = val.at("block_z").get<int>();
	map.Set(bx, by, bz, true, 0xFFFFFFFF, true);

	Vector3 v0(static_cast<float>(val.at("ray_ox").get<double>()),
	           static_cast<float>(val.at("ray_oy").get<double>()),
	           static_cast<float>(val.at("ray_oz").get<double>()));
	Vector3 dir(static_cast<float>(val.at("ray_dx").get<double>()),
	            static_cast<float>(val.at("ray_dy").get<double>()),
	            static_cast<float>(val.at("ray_dz").get<double>()));

	auto result = map.CastRay2(v0, dir, 100);

	// Bool fields — exact
	EXPECT_EQ(result.hit, val.at("hit").get<bool>());
	EXPECT_EQ(result.startSolid, val.at("startSolid").get<bool>());

	// hitPos — RAYCAST_TOL (for startSolid path, hitPos == v0)
	const auto& hp = val.at("hitPos");
	EXPECT_NEAR(static_cast<double>(result.hitPos.x), hp.at(0).get<double>(), RAYCAST_TOL);
	EXPECT_NEAR(static_cast<double>(result.hitPos.y), hp.at(1).get<double>(), RAYCAST_TOL);
	EXPECT_NEAR(static_cast<double>(result.hitPos.z), hp.at(2).get<double>(), RAYCAST_TOL);

	// hitBlock — integer, exact
	const auto& hb = val.at("hitBlock");
	EXPECT_EQ(result.hitBlock.x, hb.at(0).get<int>());
	EXPECT_EQ(result.hitBlock.y, hb.at(1).get<int>());
	EXPECT_EQ(result.hitBlock.z, hb.at(2).get<int>());

	// stepCount — exact (should be 0 for startSolid)
	int stepCount = CountRaySteps(map, v0, dir, 100);
	EXPECT_EQ(stepCount, val.at("stepCount").get<int>());
}

// RaycastFixture_ClipBox: replay ClipBox z-boundary tests.
// All clip results are bool — exact EXPECT_EQ.
TEST_F(MapTestBase, RaycastFixture_ClipBox) {
	auto j = LoadFixtureJson("map_value_lookup_009_clip_box.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	int x = val.at("x").get<int>();
	int y = val.at("y").get<int>();

	EXPECT_EQ(map.ClipBox(x, y, -1), val.at("z_neg1_clipped").get<bool>());
	EXPECT_EQ(map.ClipBox(x, y, 30),  val.at("z_30_clipped").get<bool>());
	EXPECT_EQ(map.ClipBox(x, y, 62),  val.at("z_62_clipped").get<bool>());
	EXPECT_EQ(map.ClipBox(x, y, 63),  val.at("z_63_clipped").get<bool>());
	EXPECT_EQ(map.ClipBox(x, y, 64),  val.at("z_64_clipped").get<bool>());
	EXPECT_EQ(map.ClipBox(-1, y, 30), val.at("xy_oob_clipped").get<bool>());
}

// RaycastFixture_ClipWorld: replay ClipWorld z-boundary tests.
// Key difference from ClipBox: out-of-bounds XY returns false (not clipped).
TEST_F(MapTestBase, RaycastFixture_ClipWorld) {
	auto j = LoadFixtureJson("map_value_lookup_010_clip_world.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	int x = val.at("x").get<int>();
	int y = val.at("y").get<int>();

	EXPECT_EQ(map.ClipWorld(x, y, -1), val.at("z_neg1_clipped").get<bool>());
	EXPECT_EQ(map.ClipWorld(x, y, 30),  val.at("z_30_clipped").get<bool>());
	EXPECT_EQ(map.ClipWorld(x, y, 62),  val.at("z_62_clipped").get<bool>());
	EXPECT_EQ(map.ClipWorld(x, y, 63),  val.at("z_63_clipped").get<bool>());
	EXPECT_EQ(map.ClipWorld(x, y, 64),  val.at("z_64_clipped").get<bool>());
	EXPECT_EQ(map.ClipWorld(-1, y, 30), val.at("xy_oob_clipped").get<bool>());
}
