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

#include <cstdlib>

#include <gtest/gtest.h>

#include "ToleranceMatchers.h"

// Compile/link/value verification for ToleranceMatchers.h (SCHE-05, D-16/D-17/D-18).
// EXPECT_DOUBLE_EQ is used here to compare double literals/constexprs — the D-17 ban
// is on EXPECT_FLOAT_EQ specifically, never on EXPECT_DOUBLE_EQ.

TEST(ToleranceMatchers, ConstantsCompile) {
	// The constexpr constants are accessible and carry the locked D-16 magnitudes.
	EXPECT_DOUBLE_EQ(spades::tests::POSITION_TOL, 1e-4);
	EXPECT_DOUBLE_EQ(spades::tests::ORIENTATION_TOL, 1e-5);
	EXPECT_DOUBLE_EQ(spades::tests::GRENADE_TOL, 1e-3);
	EXPECT_DOUBLE_EQ(spades::tests::RAYCAST_TOL, 1e-6);
}

TEST(ToleranceMatchers, Vec3NearMacroExpands) {
	// Duck-typed .x/.y/.z struct — the macros never reference spades::Vector3.
	struct V3 {
		double x, y, z;
	};

	V3 a = {1.0, 2.0, 3.0};
	V3 b = {1.0 + 1e-5, 2.0 + 1e-5, 3.0 + 1e-5};

	EXPECT_VEC3_NEAR(a, b, spades::tests::POSITION_TOL); // 1e-5 < 1e-4 → pass
	EXPECT_POS_NEAR(a, a);                               // identical → pass
	EXPECT_ORI_NEAR(a, a);                               // identical → pass
	EXPECT_GRENADE_NEAR(a, a);                           // identical → pass
}

TEST(ToleranceMatchers, ToleranceForFieldDispatch) {
	using spades::tests::ToleranceForField;
	EXPECT_DOUBLE_EQ(ToleranceForField("player.orientation.x"), spades::tests::ORIENTATION_TOL);
	EXPECT_DOUBLE_EQ(ToleranceForField("grenade.position.x"), spades::tests::GRENADE_TOL);
	EXPECT_DOUBLE_EQ(ToleranceForField("fuse_s"), spades::tests::GRENADE_TOL);
	EXPECT_DOUBLE_EQ(ToleranceForField("raycast.hit"), spades::tests::RAYCAST_TOL);
	EXPECT_DOUBLE_EQ(ToleranceForField("position.x"), spades::tests::POSITION_TOL);
}

TEST(ToleranceMatchers, NoExpectFloatEqInTestsDir) {
	// Programmatic D-17 ban. The pattern matches the banned matcher's call form
	// (token immediately followed by optional space then an open paren) so that
	// this guard, its comments, and grep strings that only name the bare token
	// never self-trigger. The regex itself is assembled from fragments below so
	// the literal call form does not appear verbatim in this source file.
	// grep -q exits 0 on match (BAD), non-zero on no-match (GOOD). TESTS_DIR is
	// an absolute path injected by CMake so the scan ignores the working dir.
	const std::string banned = "EXPECT_FLOAT_EQ";
	const std::string pattern = banned + "[[:space:]]*[(]";
	// Quote TESTS_DIR so a project root containing spaces does not split into
	// multiple grep arguments — an unquoted path would make grep error out
	// (non-zero exit), which EXPECT_NE(rc, 0) would misread as "no banned match"
	// and silently disable the ban guard (WR-03). CMake-generated absolute paths
	// cannot contain a single quote, so single-quoting is sufficient here.
	const std::string cmd = "grep -rqE '" + pattern + "' '" TESTS_DIR "'";
	int rc = std::system(cmd.c_str());
	EXPECT_NE(rc, 0) << "banned matcher invocation found in " TESTS_DIR
	                 << " — use EXPECT_NEAR-based matchers per D-17";
}
