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

// PROT-05 extension-negotiation replay test: load the committed proto_ext_
// fixture (kind value_lookup), HexDecode its single inputs[].bytes_hex, fold via
// ReplaySnapshot, and assert the accumulator's extensions map is populated with
// ALL advertised entries (id->version) per A3 — the portable wire fact, NOT
// NetClient's implementedExtensions filter (RESEARCH Pattern 1 / assumption A3).
// The test also rebuilds expected.value from the fold output (stringified ids)
// and asserts it equals the frozen fixture value, proving stored == fold output.
// Versions are integers — compared EXACTLY with EXPECT_EQ (no floats here, so the
// project-wide EXPECT_FLOAT_EQ ban / ToleranceMatchers are not needed).

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <Client/ProtocolCodec.h>
#include <Core/Exception.h>

#include "ProtocolReplay.h"
#include "SettingsGuard.h"

using namespace spades;
using spades::tests::HexDecode;
using spades::tests::ReplaySnapshot;
using spades::tests::WorldSnapshot;

namespace {

	// Resolve fixtures/<name> via the compile-time TESTS_DIR (= Tests/ source dir,
	// defined in Tests/CMakeLists.txt). Tiny inline loader mirroring the one in
	// ProtocolGoldenTest.cpp — duplicated here (rather than sharing a header) so this
	// test stays a self-contained translation unit. The fixtures dir sits at
	// ${TESTS_DIR}/../fixtures, so the lookup is runner-cwd-independent.
	nlohmann::json LoadFixtureJson(const std::string& name) {
		std::string path = std::string(TESTS_DIR) + "/../fixtures/" + name;
		std::ifstream f(path);
		if (!f.is_open())
			SPRaise("Cannot open fixture: %s", path.c_str());
		nlohmann::json j;
		f >> j;
		return j;
	}

	// Map a fixture protocol_version string to the codec's integer version
	// (3 = 0.75, 4 = 0.76; D-08). ExtensionInfo's wire layout is version-independent,
	// but the fold signature requires a version argument.
	int ProtocolVersionToInt(const nlohmann::json& fixture) {
		return (fixture.at("protocol_version").get<std::string>() == "0.76") ? 4 : 3;
	}

} // namespace

// Suite name "ProtocolExtensionTest" so the VALIDATION filter `ctest -R Extension`
// matches. Pins cg_unicode via SettingsGuard for parity with the other protocol
// suites (ExtensionInfo carries no strings, but the guard keeps fold setup uniform).
class ProtocolExtensionTest : public ::testing::Test {
protected:
	spades::tests::SettingsGuard guard_;
};

// PROT-05: a synthetic ExtensionInfo packet populates the accumulator's extensions
// map with ALL advertised entries (A3), and the frozen fixture value matches the
// fold output exactly.
TEST_F(ProtocolExtensionTest, ExtensionNegotiation) {
	nlohmann::json fixture = LoadFixtureJson("proto_ext_075_001.json");
	int ver = ProtocolVersionToInt(fixture);

	// Single ExtensionInfo packet (kind value_lookup → one inputs entry).
	ASSERT_EQ(fixture.at("inputs").size(), 1u);
	std::vector<std::vector<char>> packets;
	packets.push_back(HexDecode(fixture.at("inputs")[0].at("bytes_hex").get<std::string>()));

	WorldSnapshot snap = ReplaySnapshot(packets, ver);

	// The fold records ALL advertised extensions (A3 wire fact). The synthetic
	// packet advertised exactly {id 0 v1, id 192 v1}.
	ASSERT_EQ(snap.extensions.size(), 2u) << "extensions map should hold all advertised entries";
	ASSERT_EQ(snap.extensions.count(0), 1u);
	ASSERT_EQ(snap.extensions.count(192), 1u);
	EXPECT_EQ(snap.extensions.at(0), 1);     // exact int version compare
	EXPECT_EQ(snap.extensions.at(192), 1);

	// Rebuild the value_lookup payload from the fold output: a JSON object keyed by
	// the stringified extension id, value = version. This must equal the frozen
	// fixture value (proves stored fixture == fold output).
	nlohmann::json got = nlohmann::json::object();
	for (const auto& kv : snap.extensions)
		got[std::to_string(kv.first)] = kv.second;

	EXPECT_EQ(got, fixture.at("expected").at("value"))
	  << "fold-produced extensions map must match the frozen fixture value";
}
