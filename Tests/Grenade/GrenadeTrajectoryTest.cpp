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

// WEAP-03: deterministic N-tick grenade trajectory goldens — gravity + floor/wall
// bounce, water landing z>=63, fuse countdown -> Explode. Uses SnapshotGrenadeTick
// (Helpers/GrenadeSnapshot.h) per tick + RecordingWorldListener water/explode/bounce
// counters. Filled in by Plan 07-03.
//
// This Wave-1 stub keeps the zerospades_tests target compilable and ctest happy
// while Plan 07-03 authors the real generate-then-freeze fixtures.

#include <gtest/gtest.h>

TEST(DISABLED_GrenadeTrajectoryStub, Placeholder) {}
