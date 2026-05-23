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

// PROT-03/04 golden replay tests: read a committed proto_ fixture, HexDecode its
// inputs[].bytes_hex, fold via ReplaySnapshot, serialize ToJson(), and compare
// against the fixture's frozen expected snapshot IN-PROCESS. Floats are compared
// with EXPECT_NEAR(ToleranceForField(path)); ints/bools/strings exactly with
// EXPECT_EQ. EXPECT_FLOAT_EQ is banned project-wide (CTest lint NoExpectFloatEq).
// The compare_snapshots binary is exercised separately as a CTest (see
// Tests/CMakeLists.txt CompareSnapshotsProtoGolden075).

#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <Client/ProtocolCodec.h>
#include <Core/Exception.h>

#include "ProtocolReplay.h"
#include "SettingsGuard.h"
#include "ToleranceMatchers.h"

using namespace spades;
using namespace spades::client;
using spades::tests::HexDecode;
using spades::tests::ReplaySnapshot;
using spades::tests::ToleranceForField;
using spades::tests::WorldSnapshot;

namespace {

	// Resolve fixtures/<name> via the compile-time TESTS_DIR (= Tests/ source dir,
	// defined in Tests/CMakeLists.txt). The fixtures dir sits at ${TESTS_DIR}/../fixtures,
	// so the lookup is independent of the test runner's working directory.
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
	// (3 = 0.75, 4 = 0.76; D-08).
	int ProtocolVersionToInt(const nlohmann::json& fixture) {
		return (fixture.at("protocol_version").get<std::string>() == "0.76") ? 4 : 3;
	}

	// Decode every inputs[].bytes_hex into a raw packet byte vector.
	std::vector<std::vector<char>> DecodeInputPackets(const nlohmann::json& fixture) {
		std::vector<std::vector<char>> packets;
		for (const auto& in : fixture.at("inputs"))
			packets.push_back(HexDecode(in.at("bytes_hex").get<std::string>()));
		return packets;
	}

	// Recursively walk the frozen expected JSON and assert got matches. Numeric
	// fields use EXPECT_NEAR with the per-field tolerance (ToleranceForField on the
	// dotted path); everything else is an exact EXPECT_EQ. This is the in-process
	// analogue of tools/compare_snapshots and never uses the banned EXPECT_FLOAT_EQ.
	void ExpectSnapshotMatches(const nlohmann::json& want, const nlohmann::json& got,
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

	// Fold a world_snapshot fixture and assert its serialized snapshot matches the
	// frozen expected. Shared by the four world_snapshot golden tests.
	void RunWorldSnapshotGolden(const std::string& fixtureName) {
		nlohmann::json fixture = LoadFixtureJson(fixtureName);
		int ver = ProtocolVersionToInt(fixture);
		std::vector<std::vector<char>> packets = DecodeInputPackets(fixture);
		WorldSnapshot snap = ReplaySnapshot(packets, ver);
		nlohmann::json got = snap.ToJson();
		ExpectSnapshotMatches(fixture.at("expected"), got, "expected");
	}

} // namespace

// Suite name "ProtocolGoldenTest" so the VALIDATION filter `ctest -R Golden`
// matches. Pins cg_unicode="1" (Pitfall 7) for the string-bearing StateData
// teamName / ExistingPlayer name / CreatePlayer name fields.
class ProtocolGoldenTest : public ::testing::Test {
protected:
	spades::tests::SettingsGuard guard_;
};

// PROT-03: 0.75 full sequence folds to the frozen WorldSnapshot.
TEST_F(ProtocolGoldenTest, Golden_075_Sequence) {
	RunWorldSnapshotGolden("proto_golden_075_001.json");
}

// PROT-03: 0.76 full sequence (v4 sparse explicit index) folds to the frozen snapshot.
TEST_F(ProtocolGoldenTest, Golden_076_Sequence) {
	RunWorldSnapshotGolden("proto_golden_076_001.json");
}

// PROT-04: WorldUpdate v3 short branch (24B/entry, implicit index) folds correctly.
TEST_F(ProtocolGoldenTest, WorldUpdate_075) {
	RunWorldSnapshotGolden("proto_worldupdate_075_001.json");
}

// PROT-04: WorldUpdate v4 full branch (25B/entry, explicit per-entry index) folds correctly.
TEST_F(ProtocolGoldenTest, WorldUpdate_076) {
	RunWorldSnapshotGolden("proto_worldupdate_076_001.json");
}

// PROT-04: MapStart decodes the SAME mapSize regardless of protocol version. The
// MapStart wire layout is a single uint32 and is version-independent (Phase-3 D-13);
// the 0.75-vs-0.76 difference is a NetClient SEND-side effect (SendMapCached), not a
// recv-driven wire difference. This test decodes the fixture's frozen bytes under a
// reader built once and asserts the size matches expected.decoded.map_size, then
// re-decodes a fresh reader to confirm the value does not depend on which protocol
// version drives the replay.
TEST_F(ProtocolGoldenTest, MapStartBothVersions) {
	nlohmann::json fixture = LoadFixtureJson("proto_mapstart_001.json");
	std::vector<char> bytes = HexDecode(fixture.at("expected").at("bytes_hex").get<std::string>());
	uint32_t wantSize = fixture.at("expected").at("decoded").at("map_size").get<uint32_t>();

	// Decode under a reader as if driven by v3 (0.75).
	NetPacketReader r3(bytes);
	ASSERT_EQ(r3.GetType(), PacketTypeMapStart);
	MapStartPacket m3 = DecodeMapStart(r3);
	EXPECT_EQ(m3.mapSize, wantSize);

	// Decode the identical bytes again as if driven by v4 (0.76). DecodeMapStart
	// takes no version parameter — proving the wire layout is version-independent.
	// The v3/v4 difference (SendMapCached) is a NetClient send-side effect and is
	// NOT observable in a recv-driven decode/snapshot.
	NetPacketReader r4(bytes);
	ASSERT_EQ(r4.GetType(), PacketTypeMapStart);
	MapStartPacket m4 = DecodeMapStart(r4);
	EXPECT_EQ(m4.mapSize, wantSize);

	// Both versions yield the identical mapSize.
	EXPECT_EQ(m3.mapSize, m4.mapSize);
}
