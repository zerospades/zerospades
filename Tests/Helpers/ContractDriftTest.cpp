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

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "ToleranceMatchers.h"

// Drift-guard for CONTRACT.md §2 (CONTRACT-02, D-06). The portable contract states
// the four tolerance constants as a machine-readable name=value block; this test
// re-reads those four values from fixtures/CONTRACT.md and asserts each equals the
// matching constant the C++ oracle uses. If CONTRACT.md and the oracle ever diverge
// (e.g. a value is edited on one side only), this test FAILS and the build breaks,
// keeping the portable table and the running implementation in lockstep.
//
// The file is read via the CMake-injected absolute CONTRACT_PATH macro (mirroring
// the TESTS_DIR pattern) so the read never depends on the runner's working directory
// and never takes a user-supplied path (threat T-10-01).
//
// EXPECT_DOUBLE_EQ is used throughout — EXPECT_FLOAT_EQ is banned project-wide (D-17)
// and this file must not trip the NoExpectFloatEqInTestsDir guard.

namespace {

	// Parse the double assigned to `key` in a `key=value` line of `contents`.
	// Returns true and writes *out on success; returns false if the key is absent
	// or its value does not parse as a double. Tolerates leading/trailing spaces
	// around the `=` (the contract's fenced block uses none, but this is lenient).
	bool ParseNamedTolerance(const std::string& contents, const std::string& key, double* out) {
		std::istringstream stream(contents);
		std::string line;
		while (std::getline(stream, line)) {
			const std::size_t eq = line.find('=');
			if (eq == std::string::npos)
				continue;

			// Trim whitespace around the candidate key (left of '=').
			std::size_t keyBegin = line.find_first_not_of(" \t");
			if (keyBegin == std::string::npos)
				continue;
			std::size_t keyEnd = line.find_last_not_of(" \t", eq == 0 ? 0 : eq - 1);
			if (keyEnd == std::string::npos || keyEnd < keyBegin)
				continue;
			const std::string candidate = line.substr(keyBegin, keyEnd - keyBegin + 1);
			if (candidate != key)
				continue;

			// Trim whitespace around the value (right of '=').
			std::string value = line.substr(eq + 1);
			const std::size_t valBegin = value.find_first_not_of(" \t");
			if (valBegin == std::string::npos)
				return false;
			const std::size_t valEnd = value.find_last_not_of(" \t\r");
			value = value.substr(valBegin, valEnd - valBegin + 1);

			try {
				std::size_t consumed = 0;
				const double parsed = std::stod(value, &consumed);
				if (consumed != value.size())
					return false; // trailing garbage after the number
				*out = parsed;
				return true;
			} catch (...) {
				return false;
			}
		}
		return false;
	}

} // namespace

TEST(ContractToleranceDrift, MatchesHeaderConstants) {
	std::ifstream file(CONTRACT_PATH);
	ASSERT_TRUE(file.is_open()) << "cannot open CONTRACT.md at " CONTRACT_PATH;

	std::stringstream buffer;
	buffer << file.rdbuf();
	const std::string contents = buffer.str();
	ASSERT_FALSE(contents.empty()) << "CONTRACT.md is empty at " CONTRACT_PATH;

	double positionTol = 0.0;
	double orientationTol = 0.0;
	double grenadeTol = 0.0;
	double raycastTol = 0.0;

	ASSERT_TRUE(ParseNamedTolerance(contents, "position_tol", &positionTol))
	    << "CONTRACT.md drift-guard block missing/invalid key: position_tol";
	ASSERT_TRUE(ParseNamedTolerance(contents, "orientation_tol", &orientationTol))
	    << "CONTRACT.md drift-guard block missing/invalid key: orientation_tol";
	ASSERT_TRUE(ParseNamedTolerance(contents, "grenade_tol", &grenadeTol))
	    << "CONTRACT.md drift-guard block missing/invalid key: grenade_tol";
	ASSERT_TRUE(ParseNamedTolerance(contents, "raycast_tol", &raycastTol))
	    << "CONTRACT.md drift-guard block missing/invalid key: raycast_tol";

	// The portable table (CONTRACT.md §2) must equal the oracle's constants. Any
	// divergence here is the drift the guard exists to catch (D-06).
	EXPECT_DOUBLE_EQ(positionTol, spades::tests::POSITION_TOL);
	EXPECT_DOUBLE_EQ(orientationTol, spades::tests::ORIENTATION_TOL);
	EXPECT_DOUBLE_EQ(grenadeTol, spades::tests::GRENADE_TOL);
	EXPECT_DOUBLE_EQ(raycastTol, spades::tests::RAYCAST_TOL);
}
