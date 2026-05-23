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

#include <Client/Player.h>
#include <Core/Exception.h>

#include "HeadlessWorld.h"
#include "MakeFlatMap.h"
#include "PlayerPhysicsSnapshot.h"
#include "SettingsGuard.h"
#include "ToleranceMatchers.h"

namespace spades {
	namespace tests {

		// Resolve fixtures/<name> via the compile-time TESTS_DIR (= Tests/ source dir,
		// defined in Tests/CMakeLists.txt). The fixtures dir sits at ${TESTS_DIR}/../fixtures.
		// Copied verbatim from ProtocolGoldenTest.cpp:56-64.
		inline nlohmann::json LoadFixtureJson(const std::string& name) {
			std::string path = std::string(TESTS_DIR) + "/../fixtures/" + name;
			std::ifstream f(path);
			if (!f.is_open())
				SPRaise("Cannot open fixture: %s", path.c_str());
			nlohmann::json j;
			f >> j;
			return j;
		}

		// Recursively walk the frozen expected JSON and assert got matches. Numeric
		// fields use EXPECT_NEAR with the per-field tolerance (ToleranceForField on the
		// dotted path); everything else is an exact EXPECT_EQ. This is the in-process
		// analogue of tools/compare_snapshots and never uses the banned EXPECT_FLOAT_EQ.
		// Copied verbatim from ProtocolGoldenTest.cpp:84-119.
		inline void ExpectSnapshotMatches(const nlohmann::json& want, const nlohmann::json& got,
		                                  const std::string& path) {
			if (want.is_object()) {
				ASSERT_TRUE(got.is_object()) << "at " << path << ": expected object";
				for (auto& [key, wv] : want.items()) {
					ASSERT_TRUE(got.contains(key)) << "at " << path << ": missing key '" << key << "'";
					ExpectSnapshotMatches(wv, got.at(key), path + "." + key);
				}
				// Symmetric: flag extra keys the candidate emitted but the oracle did not.
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
			// Floats: per-field absolute tolerance (positions 1e-4, orientation 1e-5).
			// Integers must NOT be widened to float compare — they compare exactly so a
			// frozen integer field (id/health/team_id) catches any drift.
			if (want.is_number_float() || got.is_number_float()) {
				ASSERT_TRUE(want.is_number() && got.is_number())
				  << "at " << path << ": expected numeric";
				EXPECT_NEAR(want.get<double>(), got.get<double>(), ToleranceForField(path))
				  << "at " << path;
				return;
			}
			// Integers, bools, strings, null: exact.
			EXPECT_EQ(want, got) << "at " << path;
		}

		/**
		 * GoogleTest fixture base for all physics step-trace tests.
		 *
		 * Provides: SettingsGuard isolation (HARN-04/Pitfall 4), LoadFixtureJson,
		 * and ExpectSnapshotMatches shared by all Physics/ tests.
		 */
		class PhysicsTestBase : public ::testing::Test {
		protected:
			spades::tests::SettingsGuard guard_;
		};

	} // namespace tests
} // namespace spades
