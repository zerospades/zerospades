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

#include <cmath>
#include <string>

#include <gtest/gtest.h>

// NOTE: This header deliberately does NOT include <Core/Math.h> or any game
// headers. The EXPECT_VEC3_NEAR family uses duck-typed .x/.y/.z member access,
// so the macros work on any struct with those members. This keeps the header
// consumable by tools/compare_snapshots (Plan 05) without pulling the full
// zerospades_testable compilation unit. (Threat T-02-13)

namespace spades {
	namespace tests {

		// Per-field absolute tolerance constants (D-16). Single source of truth,
		// shared with tools/compare_snapshots via ToleranceForField() (D-18).
		constexpr double POSITION_TOL    = 1e-4; // per D-16
		constexpr double ORIENTATION_TOL = 1e-5; // per D-16
		constexpr double GRENADE_TOL     = 1e-3; // per D-16
		constexpr double RAYCAST_TOL     = 1e-6; // per D-16

		/**
		 * Field-name-substring tolerance lookup used by compare_snapshots (D-18).
		 *
		 * Dispatch order is significant: orientation, then fuse/grenade, then
		 * raycast, then the position default. Returns the absolute tolerance to
		 * apply to a float field identified by its dotted path (e.g.
		 * "player.orientation.x").
		 */
		inline double ToleranceForField(const std::string& fieldPath) {
			if (fieldPath.find("orientation") != std::string::npos)
				return ORIENTATION_TOL;
			if (fieldPath.find("fuse") != std::string::npos)
				return GRENADE_TOL;
			if (fieldPath.find("grenade") != std::string::npos)
				return GRENADE_TOL;
			if (fieldPath.find("raycast") != std::string::npos)
				return RAYCAST_TOL;
			return POSITION_TOL; // default for position/velocity/etc.
		}

	} // namespace tests
} // namespace spades

// GoogleTest helper macros (D-17). EXPECT_FLOAT_EQ is banned project-wide —
// use these absolute-tolerance matchers instead. Macros are file-scope (not
// namespace-qualified) so they are usable without a prefix.
#define EXPECT_VEC3_NEAR(a, b, tol)           \
	do {                                      \
		EXPECT_NEAR((a).x, (b).x, (tol));     \
		EXPECT_NEAR((a).y, (b).y, (tol));     \
		EXPECT_NEAR((a).z, (b).z, (tol));     \
	} while (false)

#define EXPECT_ORI_NEAR(a, b) EXPECT_VEC3_NEAR(a, b, ::spades::tests::ORIENTATION_TOL)

#define EXPECT_POS_NEAR(a, b) EXPECT_VEC3_NEAR(a, b, ::spades::tests::POSITION_TOL)

#define EXPECT_GRENADE_NEAR(a, b) EXPECT_VEC3_NEAR(a, b, ::spades::tests::GRENADE_TOL)
