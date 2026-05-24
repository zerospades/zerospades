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

// MAP-03 block-action fixture tests.
//
// Pattern: generate-then-freeze (Phase 5 established).
//   - DISABLED_BlockActionGenerate.* emit expected values from the C++ oracle.
//     Run manually once with --gtest_also_run_disabled_tests, commit the frozen
//     fixtures, then never again.
//   - BlockActionFixture_* replay tests load the frozen fixture, re-run the
//     identical scenario in-process, and assert match via EXPECT_EQ (exact).
//
// Families covered (MAP-03):
//   Create  — CreateBlock places solid block at target position with given color
//   Tool    — DestroyBlock(1 cell) via BlockActionTool removes a single block
//   Dig     — DestroyBlock(3 cells: z-1/z/z+1) via BlockActionDig
//   Grenade — DestroyBlock(27 cells: 3x3x3 cube) via BlockActionGrenade
//
// Per 06-RESEARCH.md BlockAction post-state characterization:
//   - All actions accumulate via CreateBlock/DestroyBlock then apply via ApplyBlockActions.
//   - Post-action solid state captured via IsSolid (exact bool).
//   - Color captured via GetColor for Create action (exact uint32 including health byte).
//   - Non-overlapping actions only; ordering undefined for overlaps (see 06-RESEARCH.md Risk 4).
//
// Per 06-CONTEXT.md: no player entity needed; drive World directly via HeadlessWorld.

#include <fstream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <Client/GameConstants.h>
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

} // namespace

// ===========================================================================
// DISABLED_ generator tests — run once manually, commit frozen fixtures.
// ===========================================================================

// BlockActionCreate: CreateBlock places a solid block at target position with the
// given RGB color.  ApplyBlockActions applies the pending creation.
// Expected: IsSolid(256,256,61)=true; GetColor includes health byte (0x64) in high byte.
//
// Note: flat map has z=62 as ground.  We place at z=61 (above ground but below the
// existing ground so it's in air space; the flat map leaves z<62 as air).
TEST(DISABLED_BlockActionGenerate, Create) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	// Target position: (256, 256, 61) — in air above the solid ground layer
	const IntVector3 pos{256, 256, 61};
	// RGB color for CreateBlock (World::ApplyBlockActions will OR in health=100 in high byte)
	const IntVector3 rgb{255, 128, 64}; // R=255, G=128, B=64

	// Precondition: position is air before action
	ASSERT_FALSE(map.IsSolid(pos.x, pos.y, pos.z));

	// Accumulate then apply (hw.Advance calls World::Advance → ApplyBlockActions)
	world.CreateBlock(pos, rgb);
	hw.Advance(1);

	// Capture post-action state
	bool solidAfter = map.IsSolid(pos.x, pos.y, pos.z);
	uint32_t colorAfter = map.GetColor(pos.x, pos.y, pos.z);

	auto j = BuildFixtureEnvelope("map_value_lookup_011_block_action_create");
	j["expected"]["value"] = {
	    {"x", pos.x},
	    {"y", pos.y},
	    {"z", pos.z},
	    // solid_after: CreateBlock should make this position solid
	    {"solid_after", solidAfter},
	    // color_after: full 32-bit uint stored as uint64 to avoid JSON truncation
	    // Format: 0xHHBBGGRR where HH is health byte (0x64 = 100)
	    {"color_after", static_cast<uint64_t>(colorAfter)},
	    // RGB components from the input (for replay reconstruction)
	    {"rgb_r", rgb.x},
	    {"rgb_g", rgb.y},
	    {"rgb_b", rgb.z},
	};
	WriteFixture("map_value_lookup_011_block_action_create.json", j);
}

// BlockActionTool: BlockActionTool calls DestroyBlock on a single cell.
// This is a "spade tool" destroy — removes one block.
// Expected: IsSolid(target)=false after action.
TEST(DISABLED_BlockActionGenerate, Tool) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	// Place a block first via CreateBlock+Advance so the GameMapWrapper link
	// structure is properly updated (per ClusterTest.cpp IMPORTANT comment).
	// Direct map.Set() does not update wrapper link state.
	const IntVector3 pos{100, 100, 61};
	const IntVector3 rgbGray{128, 128, 128};
	world.CreateBlock(pos, rgbGray);
	hw.Advance(1);

	// Precondition: position is solid before action
	ASSERT_TRUE(map.IsSolid(pos.x, pos.y, pos.z));

	// BlockActionTool = DestroyBlock on single cell (per NetClient.cpp:771-776)
	std::vector<IntVector3> cells;
	cells.push_back(pos);
	world.DestroyBlock(cells);
	hw.Advance(1);

	bool solidAfter = map.IsSolid(pos.x, pos.y, pos.z);

	auto j = BuildFixtureEnvelope("map_value_lookup_012_block_action_tool");
	j["expected"]["value"] = {
	    {"x", pos.x},
	    {"y", pos.y},
	    {"z", pos.z},
	    // solid_after: Tool destroy should make this position non-solid
	    {"solid_after", solidAfter},
	};
	WriteFixture("map_value_lookup_012_block_action_tool.json", j);
}

// BlockActionDig: BlockActionDig calls DestroyBlock on 3 cells (z-1, z, z+1).
// This is the spade dig — removes a vertical column of 3 blocks.
// Expected: IsSolid for all 3 cells = false after action.
TEST(DISABLED_BlockActionGenerate, Dig) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	// Target center position for the dig action
	// Per World::DestroyBlock: allowToDestroy=(pos.size()==1); for pos.size()>1,
	// z >= 62 is skipped (not >= 63). Use center at z=61 so z-1=60, z=61, z+1=62.
	// z=62 is the flat map ground — it IS destroyable (z < 62 is the multi-cell limit).
	// Actually z+1=62 satisfies z < 62? No: 62 >= 62, so it will be skipped for multi-cell.
	// Use center at z=31 to safely avoid boundary: z=30, 31, 32 all < 62 and in air.
	const int centerX = 150, centerY = 150, centerZ = 31;

	// Place all 3 blocks via CreateBlock+Advance so the GameMapWrapper link
	// structure is properly updated (per ClusterTest.cpp IMPORTANT comment).
	const IntVector3 rgbWhite{255, 255, 255};
	world.CreateBlock(IntVector3{centerX, centerY, centerZ - 1}, rgbWhite);
	world.CreateBlock(IntVector3{centerX, centerY, centerZ},     rgbWhite);
	world.CreateBlock(IntVector3{centerX, centerY, centerZ + 1}, rgbWhite);
	hw.Advance(1);

	// Preconditions
	ASSERT_TRUE(map.IsSolid(centerX, centerY, centerZ - 1));
	ASSERT_TRUE(map.IsSolid(centerX, centerY, centerZ));
	ASSERT_TRUE(map.IsSolid(centerX, centerY, centerZ + 1));

	// BlockActionDig = DestroyBlock on 3 cells (per NetClient.cpp:777-781)
	std::vector<IntVector3> cells;
	cells.push_back(IntVector3{centerX, centerY, centerZ - 1});
	cells.push_back(IntVector3{centerX, centerY, centerZ});
	cells.push_back(IntVector3{centerX, centerY, centerZ + 1});
	world.DestroyBlock(cells);
	hw.Advance(1);

	bool solidZm1 = map.IsSolid(centerX, centerY, centerZ - 1);
	bool solidZ   = map.IsSolid(centerX, centerY, centerZ);
	bool solidZp1 = map.IsSolid(centerX, centerY, centerZ + 1);

	auto j = BuildFixtureEnvelope("map_value_lookup_013_block_action_dig");
	j["expected"]["value"] = {
	    {"center_x", centerX},
	    {"center_y", centerY},
	    {"center_z", centerZ},
	    // Post-action solid state for z-1, z, z+1
	    {"solid_zm1_after", solidZm1},
	    {"solid_z_after",   solidZ},
	    {"solid_zp1_after", solidZp1},
	};
	WriteFixture("map_value_lookup_013_block_action_dig.json", j);
}

// BlockActionGrenade: BlockActionGrenade calls DestroyBlock on 27 cells (3x3x3 cube).
// Expected: IsSolid for all destroyed cells = false after action.
// Center at (200, 200, 30): offsets -1..+1 in x/y/z → all z < 62 → all destroyable.
TEST(DISABLED_BlockActionGenerate, Grenade) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	const int cx = 200, cy = 200, cz = 30;

	// Place all 27 cells in the 3x3x3 cube
	for (int dx = -1; dx <= 1; dx++)
	for (int dy = -1; dy <= 1; dy++)
	for (int dz = -1; dz <= 1; dz++)
		map.Set(cx + dx, cy + dy, cz + dz, true, 0xFFFFFFFFU);

	// Verify 27 cells placed
	for (int dx = -1; dx <= 1; dx++)
	for (int dy = -1; dy <= 1; dy++)
	for (int dz = -1; dz <= 1; dz++)
		ASSERT_TRUE(map.IsSolid(cx + dx, cy + dy, cz + dz));

	// BlockActionGrenade = DestroyBlock on 3x3x3 cube (per NetClient.cpp:782-789)
	std::vector<IntVector3> cells;
	for (int dx = -1; dx <= 1; dx++)
	for (int dy = -1; dy <= 1; dy++)
	for (int dz = -1; dz <= 1; dz++)
		cells.push_back(IntVector3{cx + dx, cy + dy, cz + dz});
	world.DestroyBlock(cells);
	hw.Advance(1);

	// Capture post-action solid state for all 27 cells
	// Store as flat array: [{dx, dy, dz, solid_after}]
	nlohmann::json cellsJson = nlohmann::json::array();
	for (int dx = -1; dx <= 1; dx++)
	for (int dy = -1; dy <= 1; dy++)
	for (int dz = -1; dz <= 1; dz++) {
		bool solidAfter = map.IsSolid(cx + dx, cy + dy, cz + dz);
		cellsJson.push_back({
		    {"dx", dx}, {"dy", dy}, {"dz", dz},
		    {"x", cx + dx}, {"y", cy + dy}, {"z", cz + dz},
		    {"solid_after", solidAfter},
		});
	}

	auto j = BuildFixtureEnvelope("map_value_lookup_014_block_action_grenade");
	j["expected"]["value"] = {
	    {"center_x", cx},
	    {"center_y", cy},
	    {"center_z", cz},
	    {"cells", cellsJson},
	};
	WriteFixture("map_value_lookup_014_block_action_grenade.json", j);
}

// ===========================================================================
// Enabled replay tests — run on every build, load frozen fixtures, assert.
// ===========================================================================

// BlockActionFixture_Create: replay CreateBlock scenario.
// Verify IsSolid=true and color matches after ApplyBlockActions.
TEST_F(MapTestBase, BlockActionFixture_Create) {
	auto j = LoadFixtureJson("map_value_lookup_011_block_action_create.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	int x = val.at("x").get<int>();
	int y = val.at("y").get<int>();
	int z = val.at("z").get<int>();
	IntVector3 pos{x, y, z};
	IntVector3 rgb{
	    val.at("rgb_r").get<int>(),
	    val.at("rgb_g").get<int>(),
	    val.at("rgb_b").get<int>(),
	};

	// Replay identical scenario
	world.CreateBlock(pos, rgb);
	hw.Advance(1);

	bool solidAfter = map.IsSolid(x, y, z);
	uint32_t colorAfter = map.GetColor(x, y, z);

	EXPECT_EQ(solidAfter, val.at("solid_after").get<bool>());
	EXPECT_EQ(static_cast<uint64_t>(colorAfter), val.at("color_after").get<uint64_t>());
}

// BlockActionFixture_Tool: replay BlockActionTool (single-cell destroy) scenario.
// Verify IsSolid=false after ApplyBlockActions.
TEST_F(MapTestBase, BlockActionFixture_Tool) {
	auto j = LoadFixtureJson("map_value_lookup_012_block_action_tool.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	int x = val.at("x").get<int>();
	int y = val.at("y").get<int>();
	int z = val.at("z").get<int>();

	// Replay: place block via CreateBlock+Advance (ensures wrapper link state)
	const IntVector3 rgbGray{128, 128, 128};
	world.CreateBlock(IntVector3{x, y, z}, rgbGray);
	hw.Advance(1);

	std::vector<IntVector3> cells;
	cells.push_back(IntVector3{x, y, z});
	world.DestroyBlock(cells);
	hw.Advance(1);

	bool solidAfter = map.IsSolid(x, y, z);
	EXPECT_EQ(solidAfter, val.at("solid_after").get<bool>());
}

// BlockActionFixture_Dig: replay BlockActionDig (3-cell z-column destroy) scenario.
// Verify IsSolid=false for z-1, z, z+1 after ApplyBlockActions.
TEST_F(MapTestBase, BlockActionFixture_Dig) {
	auto j = LoadFixtureJson("map_value_lookup_013_block_action_dig.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	int cx = val.at("center_x").get<int>();
	int cy = val.at("center_y").get<int>();
	int cz = val.at("center_z").get<int>();

	// Replay: place 3 blocks via CreateBlock+Advance (ensures wrapper link state)
	const IntVector3 rgbWhite{255, 255, 255};
	world.CreateBlock(IntVector3{cx, cy, cz - 1}, rgbWhite);
	world.CreateBlock(IntVector3{cx, cy, cz},     rgbWhite);
	world.CreateBlock(IntVector3{cx, cy, cz + 1}, rgbWhite);
	hw.Advance(1);

	std::vector<IntVector3> cells;
	cells.push_back(IntVector3{cx, cy, cz - 1});
	cells.push_back(IntVector3{cx, cy, cz});
	cells.push_back(IntVector3{cx, cy, cz + 1});
	world.DestroyBlock(cells);
	hw.Advance(1);

	EXPECT_EQ(map.IsSolid(cx, cy, cz - 1), val.at("solid_zm1_after").get<bool>());
	EXPECT_EQ(map.IsSolid(cx, cy, cz),     val.at("solid_z_after").get<bool>());
	EXPECT_EQ(map.IsSolid(cx, cy, cz + 1), val.at("solid_zp1_after").get<bool>());
}

// BlockActionFixture_Grenade: replay BlockActionGrenade (3x3x3 destroy) scenario.
// Verify solid_after for all 27 cells matches fixture.
TEST_F(MapTestBase, BlockActionFixture_Grenade) {
	auto j = LoadFixtureJson("map_value_lookup_014_block_action_grenade.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();
	auto& map = *world.GetMap();

	int cx = val.at("center_x").get<int>();
	int cy = val.at("center_y").get<int>();
	int cz = val.at("center_z").get<int>();

	// Replay: place all 27 cells then destroy 3x3x3
	for (int dx = -1; dx <= 1; dx++)
	for (int dy = -1; dy <= 1; dy++)
	for (int dz = -1; dz <= 1; dz++)
		map.Set(cx + dx, cy + dy, cz + dz, true, 0xFFFFFFFFU);

	std::vector<IntVector3> cells;
	for (int dx = -1; dx <= 1; dx++)
	for (int dy = -1; dy <= 1; dy++)
	for (int dz = -1; dz <= 1; dz++)
		cells.push_back(IntVector3{cx + dx, cy + dy, cz + dz});
	world.DestroyBlock(cells);
	hw.Advance(1);

	// Compare each cell in the fixture
	const auto& cellsJson = val.at("cells");
	ASSERT_EQ(cellsJson.size(), static_cast<size_t>(27));
	for (const auto& cell : cellsJson) {
		int x = cell.at("x").get<int>();
		int y = cell.at("y").get<int>();
		int z = cell.at("z").get<int>();
		bool expectedSolid = cell.at("solid_after").get<bool>();
		bool actualSolid = map.IsSolid(x, y, z);
		EXPECT_EQ(actualSolid, expectedSolid) << "Cell (" << x << "," << y << "," << z << ")";
	}
}
