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

// MODE-02 + MODE-03 (TC): territory ownership change, pinned-clock GetProgress
// (mid-range, not clamped), progress-bar update (value_lookup) + one full-sequence
// world-state golden (world_snapshot game_mode.mode == "tc"). Filled in by Plan 07-04.
//
// This Wave-1 stub keeps the zerospades_tests target compilable and ctest happy
// while Plan 07-04 authors the real generate-then-freeze fixtures.

#include <gtest/gtest.h>

TEST(DISABLED_TCFixtureStub, Placeholder) {}
