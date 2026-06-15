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

#pragma once

#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <Core/Exception.h>

#include "HeadlessWorld.h"
#include "MakeFlatMap.h"
#include "SettingsGuard.h"
#include "ToleranceMatchers.h"

namespace spades {
	namespace tests {

		// Resolve fixtures/<name> via TESTS_DIR (= Tests/ source dir, defined in
		// Tests/CMakeLists.txt). Fixtures live at ${TESTS_DIR}/../fixtures/.
		// Pattern verbatim from PhysicsTestBase.h / ProtocolGoldenTest.cpp.
		inline nlohmann::json LoadMapFixtureJson(const std::string& name) {
			std::string path = std::string(TESTS_DIR) + "/../fixtures/" + name;
			std::ifstream f(path);
			if (!f.is_open())
				SPRaise("Cannot open fixture: %s", path.c_str());
			nlohmann::json j;
			f >> j;
			return j;
		}

		/**
		 * Assemble the standard fixture JSON envelope for map value_lookup fixtures.
		 * Shared across all Tests/Map/ generator tests to prevent schema drift.
		 */
		inline nlohmann::json BuildMapFixtureEnvelope(const std::string& id) {
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

		/**
		 * Serialize fixture JSON to the fixtures/ directory.
		 * Path resolved via TESTS_DIR compile-time constant (Tests/CMakeLists.txt).
		 * Throws on I/O failure so generator TEST bodies abort loudly.
		 */
		inline void WriteMapFixture(const std::string& name, const nlohmann::json& j) {
			std::string path = std::string(TESTS_DIR) + "/../fixtures/" + name;
			std::ofstream f(path);
			if (!f.is_open())
				SPRaise("WriteMapFixture: cannot open for write: %s", path.c_str());
			f << j.dump(4) << "\n";
		}

		/**
		 * GoogleTest fixture base for all map/block/raycast/cluster tests.
		 *
		 * Provides: SettingsGuard isolation (HARN-04), LoadMapFixtureJson,
		 * and ExpectSnapshotMatches helpers shared by all Tests/Map/ tests.
		 *
		 * Pattern: parallel to PhysicsTestBase in Tests/Physics/PhysicsTestBase.h.
		 * Test files in Tests/Map/ inherit this via TEST_F(MapTestBase, ...).
		 */
		class MapTestBase : public ::testing::Test {
		protected:
			spades::tests::SettingsGuard guard_;

			// Convenience: load a fixture by name from fixtures/
			nlohmann::json LoadFixtureJson(const std::string& name) {
				return LoadMapFixtureJson(name);
			}
		};

	} // namespace tests
} // namespace spades
