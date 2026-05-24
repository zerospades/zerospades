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
#include "ToleranceMatchers.h"

namespace spades {
	namespace tests {

		// Recursive int-exact / float-tolerance snapshot compare. Identical in
		// behavior to PhysicsTestBase.h's ExpectSnapshotMatches, inlined here
		// because Tests/Physics is not on the Tests/GameModes include path (only
		// Tests/Helpers + Sources are; same Rule-3 precedent as WeaponTestBase.h).
		// Floats use EXPECT_NEAR with the per-field tolerance (ToleranceForField on
		// the dotted path); everything else is an exact EXPECT_EQ. Never uses the
		// banned EXPECT_FLOAT_EQ. Flags both missing and extra keys (symmetric).
		inline void ExpectSnapshotMatches(const nlohmann::json& want, const nlohmann::json& got,
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
