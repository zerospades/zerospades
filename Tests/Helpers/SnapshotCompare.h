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

#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "ToleranceMatchers.h"

namespace spades {
	namespace tests {

		// WR-02: single source of truth for the recursive int-exact / float-tolerance
		// snapshot comparator. Previously copy-pasted verbatim across
		// Tests/Weapons/WeaponTestBase.h, Tests/GameModes/GameModeTestBase.h, and
		// Tests/Grenade/GrenadeTrajectoryTest.cpp (each carrying a "Tests/Physics is
		// not on the include path" rationale). Tests/Helpers IS on every sub-tree's
		// include path (Tests/CMakeLists.txt target_include_directories), so the
		// comparator lives here and the bases include it — any future fix (NaN
		// handling, integer-vs-float coercion edge cases) is applied once.
		//
		// Numeric fields use EXPECT_NEAR with the per-field tolerance (ToleranceForField
		// on the dotted path); everything else is an exact EXPECT_EQ. Never uses the
		// banned EXPECT_FLOAT_EQ. Symmetric: flags both missing AND extra keys.
		//
		// Integers must NOT be widened to a float compare — they compare exactly so a
		// frozen integer field (id/health/team_id) catches any drift.
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

	} // namespace tests
} // namespace spades
