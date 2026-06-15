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

// MAP-05 CubeLine fixture tests.
//
// Pattern: generate-then-freeze (Phase 5 established).
//   - DISABLED_BlockLineGenerate.* emit expected values from the C++ oracle.
//     Run manually once with --gtest_also_run_disabled_tests, commit the frozen
//     fixtures, then never again.
//   - BlockLineFixture_* replay tests load the frozen fixture, re-run the
//     identical scenario in-process, and assert match via EXPECT_EQ (exact).
//
// Families covered (MAP-05):
//   CubeLine — Bresenham-3D trajectory: cell list from v1 to v2 within maxLength.
//     Test covers:
//       a) Axis-aligned line in +x direction
//       b) Diagonal line in x/y plane
//       c) 3D diagonal (all three axes change)
//
// Per 06-RESEARCH.md:
//   CubeLine returns a vector<IntVector3> (cell list) from v1 toward v2,
//   stopping at maxLength or v2 (whichever comes first).
//   The order of cells is deterministic (Bresenham-3D).
//   Comparison is order-sensitive (exact cell list match).

#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <Client/GameMap.h>
#include <Client/World.h>
#include <Core/Exception.h>

#include "HeadlessWorld.h"
#include "MapTestBase.h"
#include "MakeFlatMap.h"
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

// CubeLine: Bresenham-3D trajectory from v1 to v2 within maxLength.
//
// Three orientations tested:
//   a) Axis-aligned +x: (100,100,30) to (105,100,30), maxLength=50
//      Expected: 6 cells along x axis.
//   b) Diagonal in XY plane: (100,100,30) to (105,105,30), maxLength=50
//      Expected: cells trace a Bresenham path in x/y.
//   c) 3D diagonal: (100,100,30) to (104,103,32), maxLength=50
//      Expected: cells trace a 3D Bresenham path.
TEST(DISABLED_BlockLineGenerate, CubeLine) {
	SettingsGuard guard;
	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();

	// Helper lambda: convert cell list to JSON array
	auto cellsToJson = [](const std::vector<IntVector3>& cells) {
		nlohmann::json arr = nlohmann::json::array();
		for (const auto& c : cells)
			arr.push_back({c.x, c.y, c.z});
		return arr;
	};

	// Case a: axis-aligned +x
	IntVector3 aV1{100, 100, 30};
	IntVector3 aV2{105, 100, 30};
	auto aCells = world.CubeLine(aV1, aV2, 50);

	// Case b: XY diagonal
	IntVector3 bV1{100, 100, 30};
	IntVector3 bV2{105, 105, 30};
	auto bCells = world.CubeLine(bV1, bV2, 50);

	// Case c: 3D diagonal
	IntVector3 cV1{100, 100, 30};
	IntVector3 cV2{104, 103, 32};
	auto cCells = world.CubeLine(cV1, cV2, 50);

	auto j = BuildFixtureEnvelope("map_value_lookup_015_cubeline");
	j["expected"]["value"] = {
	    // Case a: axis-aligned +x
	    {"caseA_v1", {aV1.x, aV1.y, aV1.z}},
	    {"caseA_v2", {aV2.x, aV2.y, aV2.z}},
	    {"caseA_maxLength", 50},
	    {"caseA_cell_count", static_cast<int>(aCells.size())},
	    {"caseA_cells", cellsToJson(aCells)},
	    // Case b: XY diagonal
	    {"caseB_v1", {bV1.x, bV1.y, bV1.z}},
	    {"caseB_v2", {bV2.x, bV2.y, bV2.z}},
	    {"caseB_maxLength", 50},
	    {"caseB_cell_count", static_cast<int>(bCells.size())},
	    {"caseB_cells", cellsToJson(bCells)},
	    // Case c: 3D diagonal
	    {"caseC_v1", {cV1.x, cV1.y, cV1.z}},
	    {"caseC_v2", {cV2.x, cV2.y, cV2.z}},
	    {"caseC_maxLength", 50},
	    {"caseC_cell_count", static_cast<int>(cCells.size())},
	    {"caseC_cells", cellsToJson(cCells)},
	};
	WriteFixture("map_value_lookup_015_cubeline.json", j);
}

// ===========================================================================
// Enabled replay tests — run on every build, load frozen fixtures, assert.
// ===========================================================================

// BlockLineFixture_CubeLine: replay CubeLine for all three orientations.
// Cell list comparison is order-sensitive (exact Bresenham trajectory).
TEST_F(MapTestBase, BlockLineFixture_CubeLine) {
	auto j = LoadFixtureJson("map_value_lookup_015_cubeline.json");
	const auto& val = j.at("expected").at("value");

	auto vxl = MakeFlatMapBytes();
	HeadlessWorld hw(42, vxl);
	auto& world = hw.GetWorld();

	// Helper: compare cell list from world.CubeLine against fixture cells array
	auto checkCells = [&](const std::string& prefix, const std::vector<IntVector3>& actualCells) {
		const auto& fixtureCells = val.at(prefix + "_cells");
		ASSERT_EQ(actualCells.size(), val.at(prefix + "_cell_count").get<size_t>())
		    << "Cell count mismatch for " << prefix;
		for (size_t i = 0; i < actualCells.size(); i++) {
			EXPECT_EQ(actualCells[i].x, fixtureCells.at(i).at(0).get<int>())
			    << prefix << " cell[" << i << "].x";
			EXPECT_EQ(actualCells[i].y, fixtureCells.at(i).at(1).get<int>())
			    << prefix << " cell[" << i << "].y";
			EXPECT_EQ(actualCells[i].z, fixtureCells.at(i).at(2).get<int>())
			    << prefix << " cell[" << i << "].z";
		}
	};

	// Case a: axis-aligned +x
	{
		const auto& v1j = val.at("caseA_v1");
		const auto& v2j = val.at("caseA_v2");
		IntVector3 v1{v1j.at(0).get<int>(), v1j.at(1).get<int>(), v1j.at(2).get<int>()};
		IntVector3 v2{v2j.at(0).get<int>(), v2j.at(1).get<int>(), v2j.at(2).get<int>()};
		int maxLen = val.at("caseA_maxLength").get<int>();
		auto cells = world.CubeLine(v1, v2, maxLen);
		checkCells("caseA", cells);
	}

	// Case b: XY diagonal
	{
		const auto& v1j = val.at("caseB_v1");
		const auto& v2j = val.at("caseB_v2");
		IntVector3 v1{v1j.at(0).get<int>(), v1j.at(1).get<int>(), v1j.at(2).get<int>()};
		IntVector3 v2{v2j.at(0).get<int>(), v2j.at(1).get<int>(), v2j.at(2).get<int>()};
		int maxLen = val.at("caseB_maxLength").get<int>();
		auto cells = world.CubeLine(v1, v2, maxLen);
		checkCells("caseB", cells);
	}

	// Case c: 3D diagonal
	{
		const auto& v1j = val.at("caseC_v1");
		const auto& v2j = val.at("caseC_v2");
		IntVector3 v1{v1j.at(0).get<int>(), v1j.at(1).get<int>(), v1j.at(2).get<int>()};
		IntVector3 v2{v2j.at(0).get<int>(), v2j.at(1).get<int>(), v2j.at(2).get<int>()};
		int maxLen = val.at("caseC_maxLength").get<int>();
		auto cells = world.CubeLine(v1, v2, maxLen);
		checkCells("caseC", cells);
	}
}
