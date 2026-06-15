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

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <Core/Exception.h>

#include "SettingsGuard.h"
#include "SnapshotCompare.h" // WR-02: shared ExpectSnapshotMatches (hoisted from here)
#include "ToleranceMatchers.h"

namespace spades {
	namespace tests {

		// WR-02: ExpectSnapshotMatches now lives in Tests/Helpers/SnapshotCompare.h
		// (included above) — the per-base copy was removed to kill the four-way
		// duplication. It is still in this namespace, so TEST_F(GameModeTestBase, ...)
		// bodies call it unqualified exactly as before.

		// Resolve fixtures/<name> via the compile-time TESTS_DIR (= Tests/ source dir,
		// defined in Tests/CMakeLists.txt). Fixtures live at ${TESTS_DIR}/../fixtures/.
		// Pattern verbatim from MapTestBase.h:43-51 / WeaponTestBase.h:79-87.
		inline nlohmann::json LoadModeFixtureJson(const std::string& name) {
			std::string path = std::string(TESTS_DIR) + "/../fixtures/" + name;
			std::ifstream f(path);
			if (!f.is_open())
				SPRaise("Cannot open fixture: %s", path.c_str());
			nlohmann::json j;
			f >> j;
			return j;
		}

		/**
		 * Assemble the standard fixture JSON envelope for game-mode fixtures.
		 *
		 * @param id        Fixture id (MUST start with the "mode_" prefix — enforced
		 *                  by validate_fixtures.py PREFIX_MAP).
		 * @param kind      "value_lookup" (default) or "world_snapshot".
		 * @param behavior  "implementation_detail" (default; value_lookup) or
		 *                  "protocol_compat" (wire-driven world goldens).
		 * @param protocol  "0.75" (default) or "0.76".
		 *
		 * The expected payload is filled in by the generator after construction.
		 * Mirrors WeaponTestBase.h:101-119 with subsystem swapped to "mode".
		 */
		inline nlohmann::json
		BuildModeFixtureEnvelope(const std::string& id, const std::string& kind = "value_lookup",
		                         const std::string& behavior = "implementation_detail",
		                         const std::string& protocol = "0.75") {
			nlohmann::json j;
			j["version"] = "1.0.0";
			j["id"] = id;
			j["subsystem"] = "mode";
			j["behavior"] = behavior;
			j["seed"] = 42;
			j["protocol_version"] = protocol;
			j["map"] = {{"generator", "flat"}, {"ground_z", 62}};
			j["inputs"] = nlohmann::json::array();
			j["kind"] = kind;
			if (kind == "world_snapshot")
				j["expected"] = {{"tick", 0}, {"players", nlohmann::json::array()}};
			else
				j["expected"] = {{"value", nlohmann::json::object()}};
			return j;
		}

		/**
		 * Serialize a game-mode fixture JSON to the fixtures/ directory.
		 * Path resolved via the TESTS_DIR compile-time constant.
		 * Throws on I/O failure so generator TEST bodies abort loudly.
		 * Mirrors WeaponTestBase.h:127-133.
		 */
		inline void WriteModeFixture(const std::string& name, const nlohmann::json& j) {
			std::string path = std::string(TESTS_DIR) + "/../fixtures/" + name;
			std::ofstream f(path);
			if (!f.is_open())
				SPRaise("WriteModeFixture: cannot open for write: %s", path.c_str());
			f << j.dump(4) << "\n";
		}

		/**
		 * GoogleTest fixture base for all CTF/TC game-mode fixture tests.
		 *
		 * Provides SettingsGuard isolation (HARN-04) and LoadFixtureJson. The
		 * ExpectSnapshotMatches free function lives in this same namespace (inlined
		 * above). Test files in Tests/GameModes/ inherit this via
		 * TEST_F(GameModeTestBase, ...).
		 */
		class GameModeTestBase : public ::testing::Test {
		protected:
			spades::tests::SettingsGuard guard_;

			nlohmann::json LoadFixtureJson(const std::string& name) {
				return LoadModeFixtureJson(name);
			}
		};

	} // namespace tests
} // namespace spades
