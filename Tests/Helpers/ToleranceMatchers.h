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

#include <gtest/gtest.h>

#include "Tolerance.h"

// NOTE: This header deliberately does NOT include <Core/Math.h> or any game
// headers. The EXPECT_VEC3_NEAR family uses duck-typed .x/.y/.z member access,
// so the macros work on any struct with those members. This keeps the header
// limited to GoogleTest consumers; shared tolerance values live in Tolerance.h.
// (Threat T-02-13)

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
