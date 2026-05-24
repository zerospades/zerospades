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

// MAP-04 falling-cluster fixture tests.
//
// Pattern: generate-then-freeze (Phase 5 established).
//   - DISABLED_ClusterGenerate.* emit expected values from the C++ oracle.
//     Run manually once with --gtest_also_run_disabled_tests, commit the frozen
//     fixtures, then never again.
//   - ClusterFixture_* replay tests load the frozen fixture, re-run the
//     identical scenario in-process, and assert match via EXPECT_EQ (exact).
//
// Families covered (MAP-04 — per 06-CONTEXT.md three required cluster scenarios):
//   Pillar       — load-bearing pillar: destroy base at z=61, blocks at z=60 and z=59 float.
//                  Expected: 1 BlocksFell callback with 2 cells.
//   Connected    — single support chain: destroy middle block in a vertical column of 3
//                  above ground, leaving top block floating.
//                  Expected: 1 BlocksFell callback with 1 cell.
//   TwoIsolated  — two spatially isolated pillars: destroy both bases simultaneously.
//                  Expected: 2 separate BlocksFell callbacks with distinct cell sets
//                  (critical: NOT merged into 1 callback, per Risk 3 in 06-RESEARCH.md).
//
// All BlocksFell callbacks are captured via RecordingWorldListener::allBlocksFell
// (extended in Phase 6 Plan 01).  Per 06-RESEARCH.md Risk 3 BlocksFell Assertion Rigor:
//   - callback_count verified via EXPECT_EQ(allBlocksFell.size(), expected_count).
//   - per-callback cell sets verified via std::set<tuple> equality (unordered comparison).
//
// Coordinate system: flat map has ground at z=62; smaller z = higher in the world.
// "Above ground" means z < 62.  Ground blocks are at z=62 (solid).
// Blocks at z=61 are adjacent to the ground (z+1=62 is the solid ground).
// Blocks at z=60 are one level above that, z=59 two levels above.
//
// IMPORTANT: Blocks must be placed via World::CreateBlock + hw.Advance(1) so that
// the GameMapWrapper link structure is properly updated.  Direct GameMap::Set() calls
// do NOT update the wrapper's link state and would cause incorrect floating detection.
// hw.Advance(1) calls World::Advance(FIXED_DT) → ApplyBlockActions() → mapWrapper->AddBlock.
// Destructions similarly go through World::DestroyBlock + hw.Advance(1).
//
// Two-step pattern:
//   Step 1: CreateBlock (all positions) + hw.Advance(1) → properly links all placed blocks.
//   Step 2: DestroyBlock (base position) + hw.Advance(1) → fires BlocksFell for floating.

#include <algorithm>
#include <fstream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <Client/GameMap.h>
#include <Client/World.h>
#include <Core/Exception.h>

#include "HeadlessWorld.h"
#include "MapTestBase.h"
#include "MakeFlatMap.h"
#include "RecordingWorldListener.h"
#include "SettingsGuard.h"

using namespace spades;
using namespace spades::tests;
using namespace spades::client;

namespace {

	// Aliases for the shared helpers in MapTestBase.h.
	using spades::tests::BuildMapFixtureEnvelope;
	using spades::tests::WriteMapFixture;

	inline nlohmann::json BuildFixtureEnvelope(const std::string& id) {
		return BuildMapFixtureEnvelope(id);
	}
	inline void WriteFixture(const std::string& name, const nlohmann::json& j) {
		WriteMapFixture(name, j);
	}

	// ---------------------------------------------------------------------------
	// CaptureCallbacks: convert allBlocksFell to a JSON array of callback objects.
	// Each callback: {cells: [[x,y,z], ...]}
	// ---------------------------------------------------------------------------
	nlohmann::json CaptureCallbacks(const std::vector<std::vector<IntVector3>>& allBlocksFell) {
		nlohmann::json callbacks = nlohmann::json::array();
		for (const auto& cluster : allBlocksFell) {
			nlohmann::json cells = nlohmann::json::array();
			for (const auto& cell : cluster)
				cells.push_back({cell.x, cell.y, cell.z});
			callbacks.push_back({{"cells", cells}});
		}
		return callbacks;
	}

	// ---------------------------------------------------------------------------
	// kBlockColor: solid white block color for test placement (health=100, RGB=white).
	// ---------------------------------------------------------------------------
	const IntVector3 kBlockColor{255, 255, 255}; // RGB white

} // namespace

// ===========================================================================
// DISABLED_ generator tests — run once manually, commit frozen fixtures.
// ===========================================================================

// ClusterGenerate_Pillar: load-bearing pillar.
//
// Setup (two steps via World::CreateBlock + hw.Advance(1)):
//   Step 1: Create pillar blocks at z=61, z=60, z=59 via CreateBlock.
//           hw.Advance(1) applies creation through mapWrapper->AddBlock.
//   Step 2: Destroy base at z=61 via DestroyBlock.
//           hw.Advance(1) triggers BlocksFell for z=60 and z=59.
//
// Flat map has ground at z=62.  Pillar: (10,10,61) anchored to ground,
// (10,10,60) and (10,10,59) above.  Destroy z=61 → z=60 and z=59 float.
//
// Expected: 1 BlocksFell callback with 2 cells: {10,10,60} and {10,10,59}.
TEST(DISABLED_ClusterGenerate, Pillar) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	const int px = 10, py = 10;

	// Step 1: place pillar via CreateBlock → Advance (links blocks into wrapper)
	world.CreateBlock(IntVector3{px, py, 61}, kBlockColor);
	world.CreateBlock(IntVector3{px, py, 60}, kBlockColor);
	world.CreateBlock(IntVector3{px, py, 59}, kBlockColor);
	hw.Advance(1);

	// Preconditions: all three blocks now solid
	ASSERT_TRUE(map.IsSolid(px, py, 61));
	ASSERT_TRUE(map.IsSolid(px, py, 60));
	ASSERT_TRUE(map.IsSolid(px, py, 59));

	// Step 2: destroy base at z=61 → z=60 and z=59 become floating
	std::vector<IntVector3> cells;
	cells.push_back(IntVector3{px, py, 61});
	world.DestroyBlock(cells);
	hw.Advance(1);

	auto& listener = hw.GetListener();

	// Capture fixture
	// floating_z_1 and floating_z_2 are stored explicitly so an independent
	// runner (e.g. Rust) can reconstruct the pillar geometry from JSON alone.
	auto j = BuildFixtureEnvelope("map_value_lookup_016_cluster_pillar");
	j["expected"]["value"] = {
	    {"pillar_x", px},
	    {"pillar_y", py},
	    {"destroyed_z", 61},
	    {"floating_z_1", 60}, // destroyedZ - 1
	    {"floating_z_2", 59}, // destroyedZ - 2
	    {"callback_count", static_cast<int>(listener.allBlocksFell.size())},
	    {"callbacks", CaptureCallbacks(listener.allBlocksFell)},
	};
	WriteFixture("map_value_lookup_016_cluster_pillar.json", j);
}

// ClusterGenerate_Connected: single block floating after its support is removed.
//
// Setup:
//   Step 1: Create blocks at z=61 (anchored to ground) and z=60 (touching z=61).
//           hw.Advance(1) applies creation.
//   Step 2: Destroy z=61. hw.Advance(1) triggers BlocksFell for z=60.
//
// Expected: 1 BlocksFell callback with 1 cell: {20,20,60}.
TEST(DISABLED_ClusterGenerate, Connected) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	const int px = 20, py = 20;

	// Step 1: place two blocks above ground
	world.CreateBlock(IntVector3{px, py, 61}, kBlockColor);
	world.CreateBlock(IntVector3{px, py, 60}, kBlockColor);
	hw.Advance(1);

	ASSERT_TRUE(map.IsSolid(px, py, 61));
	ASSERT_TRUE(map.IsSolid(px, py, 60));

	// Step 2: destroy z=61 → z=60 loses ground connection and floats
	std::vector<IntVector3> cells;
	cells.push_back(IntVector3{px, py, 61});
	world.DestroyBlock(cells);
	hw.Advance(1);

	auto& listener = hw.GetListener();

	// Capture fixture
	auto j = BuildFixtureEnvelope("map_value_lookup_017_cluster_connected");
	j["expected"]["value"] = {
	    {"pillar_x", px},
	    {"pillar_y", py},
	    {"destroyed_z", 61},
	    {"floating_z", 60},
	    {"callback_count", static_cast<int>(listener.allBlocksFell.size())},
	    {"callbacks", CaptureCallbacks(listener.allBlocksFell)},
	};
	WriteFixture("map_value_lookup_017_cluster_connected.json", j);
}

// ClusterGenerate_TwoIsolated: two separate pillars, both bases destroyed simultaneously.
//
// Setup:
//   Step 1: Create blocks at p1=(10,10) z=61,60 and p2=(50,50) z=61,60.
//           hw.Advance(1) applies creation.
//   Step 2: Destroy both bases (z=61) via single multi-cell DestroyBlock.
//           hw.Advance(1) → 2 separate BlocksFell callbacks.
//
// Expected: 2 separate callbacks (NOT merged), each with 1 cell.
// This is the critical Rust parity test (Risk 3 in 06-RESEARCH.md).
TEST(DISABLED_ClusterGenerate, TwoIsolated) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	const int p1x = 10, p1y = 10;
	const int p2x = 50, p2y = 50;

	// Step 1: create both pillar sets
	world.CreateBlock(IntVector3{p1x, p1y, 61}, kBlockColor);
	world.CreateBlock(IntVector3{p1x, p1y, 60}, kBlockColor);
	world.CreateBlock(IntVector3{p2x, p2y, 61}, kBlockColor);
	world.CreateBlock(IntVector3{p2x, p2y, 60}, kBlockColor);
	hw.Advance(1);

	ASSERT_TRUE(map.IsSolid(p1x, p1y, 61));
	ASSERT_TRUE(map.IsSolid(p1x, p1y, 60));
	ASSERT_TRUE(map.IsSolid(p2x, p2y, 61));
	ASSERT_TRUE(map.IsSolid(p2x, p2y, 60));

	// Step 2: destroy both bases simultaneously
	// Per World::DestroyBlock: allowToDestroy=(pos.size()==1) so multi-cell allows z<62.
	// Both z=61 < 62 → both will be destroyed.
	std::vector<IntVector3> cells;
	cells.push_back(IntVector3{p1x, p1y, 61});
	cells.push_back(IntVector3{p2x, p2y, 61});
	world.DestroyBlock(cells);
	hw.Advance(1);

	auto& listener = hw.GetListener();

	// Capture fixture
	auto j = BuildFixtureEnvelope("map_value_lookup_018_cluster_two_isolated");
	j["expected"]["value"] = {
	    {"p1_x", p1x}, {"p1_y", p1y},
	    {"p2_x", p2x}, {"p2_y", p2y},
	    {"destroyed_z", 61},
	    {"floating_z", 60},
	    {"callback_count", static_cast<int>(listener.allBlocksFell.size())},
	    {"callbacks", CaptureCallbacks(listener.allBlocksFell)},
	};
	WriteFixture("map_value_lookup_018_cluster_two_isolated.json", j);
}

// ===========================================================================
// Enabled replay tests — run on every build, load frozen fixtures, assert.
// ===========================================================================

// ClusterFixture_Pillar: replay pillar scenario.
// Verify: callback_count == 1; callback[0] cell set matches fixture.
TEST_F(MapTestBase, ClusterFixture_Pillar) {
	auto j = LoadFixtureJson("map_value_lookup_016_cluster_pillar.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	int px = val.at("pillar_x").get<int>();
	int py = val.at("pillar_y").get<int>();
	int destroyedZ = val.at("destroyed_z").get<int>();
	int floatingZ1 = val.at("floating_z_1").get<int>();
	int floatingZ2 = val.at("floating_z_2").get<int>();

	// Replay step 1: place pillar from fixture coordinates (all three z-levels)
	world.CreateBlock(IntVector3{px, py, destroyedZ},  kBlockColor);
	world.CreateBlock(IntVector3{px, py, floatingZ1},  kBlockColor);
	world.CreateBlock(IntVector3{px, py, floatingZ2},  kBlockColor);
	hw.Advance(1);

	// Replay step 2: destroy base
	std::vector<IntVector3> cells;
	cells.push_back(IntVector3{px, py, destroyedZ});
	world.DestroyBlock(cells);
	hw.Advance(1);

	auto& listener = hw.GetListener();

	// Verify callback count — ASSERT so loop below doesn't run on mismatch
	int expectedCount = val.at("callback_count").get<int>();
	ASSERT_EQ(static_cast<int>(listener.allBlocksFell.size()), expectedCount);

	// Verify per-callback cell sets (unordered comparison via set<tuple>)
	const auto& fixtureCallbacks = val.at("callbacks");
	ASSERT_EQ(fixtureCallbacks.size(), static_cast<size_t>(expectedCount));
	for (size_t i = 0; i < listener.allBlocksFell.size(); i++) {
		std::set<std::tuple<int, int, int>> actualSet, expectedSet;
		for (const auto& cell : listener.allBlocksFell[i])
			actualSet.insert({cell.x, cell.y, cell.z});
		for (const auto& cell : fixtureCallbacks.at(i).at("cells"))
			expectedSet.insert(
			    {cell.at(0).get<int>(), cell.at(1).get<int>(), cell.at(2).get<int>()});
		EXPECT_EQ(actualSet, expectedSet) << "Callback " << i << " cell set mismatch";
	}
}

// ClusterFixture_Connected: replay connected scenario (single floating block).
// Verify: callback_count == 1; callback[0] has exactly 1 cell matching fixture.
TEST_F(MapTestBase, ClusterFixture_Connected) {
	auto j = LoadFixtureJson("map_value_lookup_017_cluster_connected.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	int px = val.at("pillar_x").get<int>();
	int py = val.at("pillar_y").get<int>();
	int destroyedZ = val.at("destroyed_z").get<int>();
	int floatingZ = val.at("floating_z").get<int>();

	// Replay step 1: place the two blocks
	world.CreateBlock(IntVector3{px, py, destroyedZ}, kBlockColor);
	world.CreateBlock(IntVector3{px, py, floatingZ},  kBlockColor);
	hw.Advance(1);

	// Replay step 2: destroy middle block
	std::vector<IntVector3> cells;
	cells.push_back(IntVector3{px, py, destroyedZ});
	world.DestroyBlock(cells);
	hw.Advance(1);

	auto& listener = hw.GetListener();

	// Verify callback count — ASSERT so loop below doesn't run on mismatch
	int expectedCount = val.at("callback_count").get<int>();
	ASSERT_EQ(static_cast<int>(listener.allBlocksFell.size()), expectedCount);

	// Verify per-callback cell sets
	const auto& fixtureCallbacks = val.at("callbacks");
	ASSERT_EQ(fixtureCallbacks.size(), static_cast<size_t>(expectedCount));
	for (size_t i = 0; i < listener.allBlocksFell.size(); i++) {
		std::set<std::tuple<int, int, int>> actualSet, expectedSet;
		for (const auto& cell : listener.allBlocksFell[i])
			actualSet.insert({cell.x, cell.y, cell.z});
		for (const auto& cell : fixtureCallbacks.at(i).at("cells"))
			expectedSet.insert(
			    {cell.at(0).get<int>(), cell.at(1).get<int>(), cell.at(2).get<int>()});
		EXPECT_EQ(actualSet, expectedSet) << "Callback " << i << " cell set mismatch";
	}
}

// ClusterFixture_TwoIsolated: replay two isolated pillars scenario.
// Verify: callback_count == 2; each callback has 1 cell (distinct, not merged).
// This is the critical Rust parity test — merging into 1 callback would fail here.
TEST_F(MapTestBase, ClusterFixture_TwoIsolated) {
	auto j = LoadFixtureJson("map_value_lookup_018_cluster_two_isolated.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	int p1x = val.at("p1_x").get<int>();
	int p1y = val.at("p1_y").get<int>();
	int p2x = val.at("p2_x").get<int>();
	int p2y = val.at("p2_y").get<int>();
	int destroyedZ = val.at("destroyed_z").get<int>();
	int floatingZ = val.at("floating_z").get<int>();

	// Replay step 1: place both pillar sets
	world.CreateBlock(IntVector3{p1x, p1y, destroyedZ}, kBlockColor);
	world.CreateBlock(IntVector3{p1x, p1y, floatingZ},  kBlockColor);
	world.CreateBlock(IntVector3{p2x, p2y, destroyedZ}, kBlockColor);
	world.CreateBlock(IntVector3{p2x, p2y, floatingZ},  kBlockColor);
	hw.Advance(1);

	// Replay step 2: destroy both bases simultaneously
	std::vector<IntVector3> cells;
	cells.push_back(IntVector3{p1x, p1y, destroyedZ});
	cells.push_back(IntVector3{p2x, p2y, destroyedZ});
	world.DestroyBlock(cells);
	hw.Advance(1);

	auto& listener = hw.GetListener();

	// Verify callback count (2 separate callbacks, not 1 merged) — ASSERT to halt on mismatch
	int expectedCount = val.at("callback_count").get<int>();
	ASSERT_EQ(static_cast<int>(listener.allBlocksFell.size()), expectedCount);

	// Verify per-callback cell sets (unordered comparison per 06-RESEARCH.md Risk 3)
	const auto& fixtureCallbacks = val.at("callbacks");
	ASSERT_EQ(fixtureCallbacks.size(), static_cast<size_t>(expectedCount));
	for (size_t i = 0; i < listener.allBlocksFell.size(); i++) {
		std::set<std::tuple<int, int, int>> actualSet, expectedSet;
		for (const auto& cell : listener.allBlocksFell[i])
			actualSet.insert({cell.x, cell.y, cell.z});
		for (const auto& cell : fixtureCallbacks.at(i).at("cells"))
			expectedSet.insert(
			    {cell.at(0).get<int>(), cell.at(1).get<int>(), cell.at(2).get<int>()});
		EXPECT_EQ(actualSet, expectedSet) << "Callback " << i << " cell set mismatch";
	}

	// Additional: verify the two callback cell sets do NOT overlap
	// (merging would put both cells in one callback)
	if (listener.allBlocksFell.size() == 2) {
		std::set<std::tuple<int, int, int>> set0, set1;
		for (const auto& cell : listener.allBlocksFell[0])
			set0.insert({cell.x, cell.y, cell.z});
		for (const auto& cell : listener.allBlocksFell[1])
			set1.insert({cell.x, cell.y, cell.z});

		// Intersection must be empty — clusters are disjoint
		std::vector<std::tuple<int, int, int>> intersection;
		std::set_intersection(set0.begin(), set0.end(), set1.begin(), set1.end(),
		                      std::back_inserter(intersection));
		EXPECT_TRUE(intersection.empty())
		    << "Two cluster callbacks must not overlap (clusters must not be merged)";
	}
}
