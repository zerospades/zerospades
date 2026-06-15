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
#include "SnapshotCompare.h"
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

		// ExpectSnapshotMatches is provided by SnapshotCompare.h (WR-02: single source of
		// truth — formerly duplicated here and in the Weapon/GameMode/Grenade bases).

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
