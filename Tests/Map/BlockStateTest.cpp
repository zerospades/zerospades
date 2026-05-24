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

// MAP-01 block-state fixture tests.
//
// Pattern: generate-then-freeze (Phase 5 established).
//   - DISABLED_BlockStateGenerate.* emit expected values from the C++ oracle.
//     Run manually once with --gtest_also_run_disabled_tests, commit the frozen
//     fixtures, then never again.
//   - BlockStateFixture.* replay tests load the frozen fixture, re-run the
//     identical scenario in-process, and assert match via EXPECT_EQ (exact).
//
// Families covered (MAP-01):
//   IsSolid      — in-bounds solid/non-solid queries
//   GetColor     — full 32-bit color including health byte (0xHHBBGGRR)
//   GetSolidMap  — uint64 bitmask for solid columns
//   HasNeighbors — 6-face adjacency (±x ±y ±z only, not diagonal)
//   IsSurface    — solid block with at least one air neighbor (6-face)

#include <fstream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <Client/GameMap.h>
#include <Client/World.h>

#include "HeadlessWorld.h"
#include "MapTestBase.h"
#include "MakeFlatMap.h"
#include "SettingsGuard.h"

using namespace spades;
using namespace spades::tests;
using namespace spades::client;

namespace {

	// ---------------------------------------------------------------------------
	// BuildFixtureEnvelope: assemble the full fixture JSON envelope for map
	// value_lookup fixtures.  Mirrors PhysicsFixtureTest BuildFixtureEnvelope
	// but uses subsystem="map" and kind="value_lookup".
	// ---------------------------------------------------------------------------
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

	// ---------------------------------------------------------------------------
	// WriteFixture: serialize fixture JSON to fixtures/ directory.
	// Path resolved via TESTS_DIR compile-time constant (Tests/CMakeLists.txt).
	// ---------------------------------------------------------------------------
	void WriteFixture(const std::string& name, const nlohmann::json& j) {
		std::string path = std::string(TESTS_DIR) + "/../fixtures/" + name;
		std::ofstream f(path);
		ASSERT_TRUE(f.is_open()) << "Cannot open for write: " << path;
		f << j.dump(4) << "\n";
	}

} // namespace

// ===========================================================================
// DISABLED_ generator tests — run once manually, commit frozen fixtures.
// ===========================================================================

// IsSolid: flat map ground at z=62 is solid; z=30 (air) is not.
TEST(DISABLED_BlockStateGenerate, IsSolid) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// In-bounds solid block (flat map ground layer)
	bool solidAtGround = map.IsSolid(256, 256, 62);
	// In-bounds non-solid block (air above ground)
	bool solidAtAir = map.IsSolid(256, 256, 30);

	auto j = BuildFixtureEnvelope("map_value_lookup_001_is_solid");
	j["expected"]["value"] = {
	    {"query_solid_x", 256},
	    {"query_solid_y", 256},
	    {"query_solid_z", 62},
	    {"solid_at_ground", solidAtGround},
	    {"query_air_x", 256},
	    {"query_air_y", 256},
	    {"query_air_z", 30},
	    {"solid_at_air", solidAtAir},
	};
	WriteFixture("map_value_lookup_001_is_solid.json", j);
}

// GetColor: Set a block with health byte encoded in color (0xHHBBGGRR).
// health=100 (0x64), RGB=0xFF8040 → full 32-bit value = 0x64FF8040.
// The full 32-bit value including health byte must round-trip exactly.
TEST(DISABLED_BlockStateGenerate, GetColor) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// Place a block with a distinctive color including health byte.
	// Color format: 0xHHBBGGRR (HH = health, up to 100 = 0x64)
	const uint32_t kTestColor = 0x64FF8040U;
	map.Set(100, 100, 40, true, kTestColor);
	uint32_t got = map.GetColor(100, 100, 40);

	auto j = BuildFixtureEnvelope("map_value_lookup_002_get_color");
	j["expected"]["value"] = {
	    {"x", 100},
	    {"y", 100},
	    {"z", 40},
	    // Store as unsigned integer for exact JSON round-trip
	    {"color_set", static_cast<uint64_t>(kTestColor)},
	    {"color_got", static_cast<uint64_t>(got)},
	    // Health byte is the high byte (bits 31-24).
	    // Value 0x64 = 100 decimal (max health).
	    {"health_byte", static_cast<uint64_t>((got >> 24) & 0xFF)},
	};
	WriteFixture("map_value_lookup_002_get_color.json", j);
}

// GetSolidMap: place three blocks in a column at (100, 100, 60-62).
// GetSolidMap returns the uint64 bitmask of all solid z-levels in the column.
// Bits 60, 61, 62 (plus existing flat-map ground bits) must be set.
TEST(DISABLED_BlockStateGenerate, GetSolidMap) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// Flat map already has z=62 solid; add z=60 and z=61.
	map.Set(100, 100, 60, true, 0xFFFFFFFF);
	map.Set(100, 100, 61, true, 0xFFFFFFFF);
	// z=62 already solid from flat map

	uint64_t bitmask = map.GetSolidMap(100, 100);

	auto j = BuildFixtureEnvelope("map_value_lookup_003_get_solid_map");
	j["expected"]["value"] = {
	    {"x", 100},
	    {"y", 100},
	    // JSON doesn't support uint64; store as string to avoid truncation.
	    // The replay test will read it back and compare via std::stoull.
	    {"solid_map_hex", [&] {
	         char buf[32];
	         std::snprintf(buf, sizeof(buf), "0x%016llX", (unsigned long long)bitmask);
	         return std::string(buf);
	     }()},
	    // Also store the decimal string for human readability in fixture review.
	    {"solid_map_dec", std::to_string(bitmask)},
	    // Verify the three placed bits are set (sanity booleans).
	    {"bit60_set", (bitmask >> 60 & 1ULL) != 0},
	    {"bit61_set", (bitmask >> 61 & 1ULL) != 0},
	    {"bit62_set", (bitmask >> 62 & 1ULL) != 0},
	};
	WriteFixture("map_value_lookup_003_get_solid_map.json", j);
}

// HasNeighbors: 6-face adjacency detection.
//
// Case A: isolated block at (10, 10, 10) with all 6 faces air → false.
// Case B: same block with solid at (11, 10, 10) → true.
//
// Corner-adjacency per 06-RESEARCH.md Risk 1:
// Case C: block at (10, 10, 10) with solid ONLY at diagonal (11, 11, 10) — not
//         a direct face neighbor — must return false (6-face only, no diagonals).
TEST(DISABLED_BlockStateGenerate, HasNeighbors) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// Case A: isolated block (no direct neighbors)
	map.Set(10, 10, 10, true, 0xFFFFFFFF);
	bool caseA = map.HasNeighbors(10, 10, 10);

	// Case B: add a direct +x neighbor
	map.Set(11, 10, 10, true, 0xFFFFFFFF);
	bool caseB = map.HasNeighbors(10, 10, 10);

	// Case C: corner-adjacency — block at (20, 20, 20) with solid ONLY at
	// diagonal (21, 21, 20).  HasNeighbors checks ±x, ±y, ±z only (6-face).
	// (20+1, 20, 20) air, (20-1, 20, 20) air, (20, 20+1, 20) air,
	// (20, 20-1, 20) air, (20, 20, 20+1) air, (20, 20, 20-1) air → false.
	map.Set(20, 20, 20, true, 0xFFFFFFFF);
	map.Set(21, 21, 20, true, 0xFFFFFFFF); // diagonal neighbor only
	bool caseC = map.HasNeighbors(20, 20, 20);

	auto j = BuildFixtureEnvelope("map_value_lookup_004_has_neighbors");
	j["expected"]["value"] = {
	    // Case A: isolated block
	    {"caseA_x", 10},
	    {"caseA_y", 10},
	    {"caseA_z", 10},
	    {"caseA_has_neighbors", caseA},
	    // Case B: direct +x neighbor added
	    {"caseB_neighbor_x", 11},
	    {"caseB_neighbor_y", 10},
	    {"caseB_neighbor_z", 10},
	    {"caseB_has_neighbors", caseB},
	    // Case C: diagonal neighbor only — 6-face should be false
	    {"caseC_x", 20},
	    {"caseC_y", 20},
	    {"caseC_z", 20},
	    {"caseC_diagonal_x", 21},
	    {"caseC_diagonal_y", 21},
	    {"caseC_diagonal_z", 20},
	    {"caseC_has_neighbors", caseC},
	};
	WriteFixture("map_value_lookup_004_has_neighbors.json", j);
}

// IsSurface: solid block with at least one air neighbor (6-face) → true.
//
// Case A: isolated solid block at (30, 30, 30) — all 6 faces air → surface.
// Case B: block at (30, 30, 30) surrounded by solids on all 6 faces → not surface.
TEST(DISABLED_BlockStateGenerate, IsSurface) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// Case A: isolated solid
	map.Set(30, 30, 30, true, 0xFFFFFFFF);
	bool caseA = map.IsSurface(30, 30, 30);

	// Case B: surround with solids on all 6 faces
	map.Set(31, 30, 30, true, 0xFFFFFFFF);
	map.Set(29, 30, 30, true, 0xFFFFFFFF);
	map.Set(30, 31, 30, true, 0xFFFFFFFF);
	map.Set(30, 29, 30, true, 0xFFFFFFFF);
	map.Set(30, 30, 31, true, 0xFFFFFFFF);
	map.Set(30, 30, 29, true, 0xFFFFFFFF);
	bool caseB = map.IsSurface(30, 30, 30);

	auto j = BuildFixtureEnvelope("map_value_lookup_005_is_surface");
	j["expected"]["value"] = {
	    // Case A: isolated — all 6 faces air → surface
	    {"caseA_x", 30},
	    {"caseA_y", 30},
	    {"caseA_z", 30},
	    {"caseA_is_surface", caseA},
	    // Case B: all 6 faces solid → not surface
	    {"caseB_x", 30},
	    {"caseB_y", 30},
	    {"caseB_z", 30},
	    {"caseB_is_surface", caseB},
	};
	WriteFixture("map_value_lookup_005_is_surface.json", j);
}

// ===========================================================================
// Enabled replay tests — run on every build, load frozen fixtures, assert.
// ===========================================================================

TEST_F(MapTestBase, BlockStateFixture_IsSolid) {
	auto j = LoadFixtureJson("map_value_lookup_001_is_solid.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// Replay: same flat map, no extra blocks placed
	bool solidAtGround = map.IsSolid(val.at("query_solid_x").get<int>(),
	                                  val.at("query_solid_y").get<int>(),
	                                  val.at("query_solid_z").get<int>());
	bool solidAtAir = map.IsSolid(val.at("query_air_x").get<int>(),
	                               val.at("query_air_y").get<int>(),
	                               val.at("query_air_z").get<int>());

	EXPECT_EQ(solidAtGround, val.at("solid_at_ground").get<bool>());
	EXPECT_EQ(solidAtAir, val.at("solid_at_air").get<bool>());
}

TEST_F(MapTestBase, BlockStateFixture_GetColor) {
	auto j = LoadFixtureJson("map_value_lookup_002_get_color.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	int x = val.at("x").get<int>();
	int y = val.at("y").get<int>();
	int z = val.at("z").get<int>();
	uint32_t kTestColor = static_cast<uint32_t>(val.at("color_set").get<uint64_t>());

	// Replay: place block then query color
	map.Set(x, y, z, true, kTestColor);
	uint32_t got = map.GetColor(x, y, z);

	// Full 32-bit value must match exactly (including health byte in high byte)
	EXPECT_EQ(static_cast<uint64_t>(got), val.at("color_got").get<uint64_t>());
	// Health byte (bits 31-24) must be preserved
	EXPECT_EQ(static_cast<uint64_t>((got >> 24) & 0xFFU),
	          val.at("health_byte").get<uint64_t>());
}

TEST_F(MapTestBase, BlockStateFixture_GetSolidMap) {
	auto j = LoadFixtureJson("map_value_lookup_003_get_solid_map.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	int x = val.at("x").get<int>();
	int y = val.at("y").get<int>();

	// Replay: place same blocks as generator
	map.Set(x, y, 60, true, 0xFFFFFFFF);
	map.Set(x, y, 61, true, 0xFFFFFFFF);
	// z=62 already solid from flat map

	uint64_t got = map.GetSolidMap(x, y);

	// Compare via the stored hex string to avoid JSON integer truncation
	std::string expected_hex = val.at("solid_map_hex").get<std::string>();
	uint64_t expected = static_cast<uint64_t>(std::stoull(expected_hex, nullptr, 16));
	EXPECT_EQ(got, expected);

	// Sanity: individual bit checks must agree with the stored booleans
	EXPECT_EQ((got >> 60 & 1ULL) != 0, val.at("bit60_set").get<bool>());
	EXPECT_EQ((got >> 61 & 1ULL) != 0, val.at("bit61_set").get<bool>());
	EXPECT_EQ((got >> 62 & 1ULL) != 0, val.at("bit62_set").get<bool>());
}

TEST_F(MapTestBase, BlockStateFixture_HasNeighbors) {
	auto j = LoadFixtureJson("map_value_lookup_004_has_neighbors.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// Case A: isolated block
	map.Set(10, 10, 10, true, 0xFFFFFFFF);
	bool caseA = map.HasNeighbors(10, 10, 10);
	EXPECT_EQ(caseA, val.at("caseA_has_neighbors").get<bool>());

	// Case B: add direct +x neighbor
	map.Set(11, 10, 10, true, 0xFFFFFFFF);
	bool caseB = map.HasNeighbors(10, 10, 10);
	EXPECT_EQ(caseB, val.at("caseB_has_neighbors").get<bool>());

	// Case C: diagonal neighbor only (should still be false — 6-face)
	map.Set(20, 20, 20, true, 0xFFFFFFFF);
	map.Set(21, 21, 20, true, 0xFFFFFFFF);
	bool caseC = map.HasNeighbors(20, 20, 20);
	EXPECT_EQ(caseC, val.at("caseC_has_neighbors").get<bool>());
}

TEST_F(MapTestBase, BlockStateFixture_IsSurface) {
	auto j = LoadFixtureJson("map_value_lookup_005_is_surface.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& map = *hw.GetWorld().GetMap();

	// Case A: isolated solid (all 6 faces air → surface)
	map.Set(30, 30, 30, true, 0xFFFFFFFF);
	bool caseA = map.IsSurface(30, 30, 30);
	EXPECT_EQ(caseA, val.at("caseA_is_surface").get<bool>());

	// Case B: surround with solids on all 6 faces (→ not surface)
	map.Set(31, 30, 30, true, 0xFFFFFFFF);
	map.Set(29, 30, 30, true, 0xFFFFFFFF);
	map.Set(30, 31, 30, true, 0xFFFFFFFF);
	map.Set(30, 29, 30, true, 0xFFFFFFFF);
	map.Set(30, 30, 31, true, 0xFFFFFFFF);
	map.Set(30, 30, 29, true, 0xFFFFFFFF);
	bool caseB = map.IsSurface(30, 30, 30);
	EXPECT_EQ(caseB, val.at("caseB_is_surface").get<bool>());
}
