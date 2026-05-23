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

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <Client/ProtocolCodec.h>
#include <Core/Debug.h>
#include <Core/Exception.h>

#include "ProtocolReplay.h"
#include "SettingsGuard.h"
#include "ToleranceMatchers.h"

using namespace spades;
using namespace spades::client;
using spades::tests::HexDecode;
using spades::tests::PlayerState;
using spades::tests::ReplaySnapshot;
using spades::tests::ToolName;
using spades::tests::WeaponName;
using spades::tests::WorldSnapshot;

// [Rule 3 - Blocking issue] The two-arg spades::Exception(file,line,fmt,...) ctor
// (the form SPRaise expands to) dereferences reflection::Backtrace::GetGlobalBacktrace(),
// which returns NULL until Backtrace::StartBacktrace() runs (SPADES_USE_TLS path,
// Debug.cpp:64-66). The real app calls it at startup (Gui/Main.cpp:313); the gtest_main
// process never does, so any SPRaise throw SIGSEGVs before EXPECT_THROW can catch it.
// Start it once for the whole test process. Idempotent (just sets a static bool).
// Test-only fix — no production code changed (ProtocolCodec/Exception frozen).
class BacktraceEnvironment : public ::testing::Environment {
public:
	void SetUp() override { spades::reflection::Backtrace::StartBacktrace(); }
};
static ::testing::Environment* const kBacktraceEnv =
  ::testing::AddGlobalTestEnvironment(new BacktraceEnvironment);

// Fixture pins cg_unicode="1" so string-bearing packets (ExistingPlayer/CreatePlayer
// name, StateData teamName) decode deterministically through CP437/UTF framing.
class ProtocolReplayUnitTest : public ::testing::Test {
protected:
	spades::tests::SettingsGuard guard_;

	// Append a writer's encoded bytes (incl. type tag at [0]) as one packet input.
	static void Push(std::vector<std::vector<char>>& packets, NetPacketWriter w) {
		packets.push_back(w.GetData());
	}
};

// ---------------------------------------------------------------------------
// HexDecode (Pitfall 6): round-trips valid hex; rejects odd-length / non-hex.
// ---------------------------------------------------------------------------

TEST_F(ProtocolReplayUnitTest, HexDecodeRoundTripsKnownString) {
	std::vector<char> got = HexDecode("0900Ff00aB");
	std::vector<char> want = {(char)0x09, (char)0x00, (char)0xFF, (char)0x00, (char)0xAB};
	EXPECT_EQ(got, want);
}

TEST_F(ProtocolReplayUnitTest, HexDecodeEmptyIsEmpty) {
	EXPECT_TRUE(HexDecode("").empty());
}

TEST_F(ProtocolReplayUnitTest, HexDecodeRejectsOddLength) {
	EXPECT_THROW(HexDecode("090"), spades::Exception);
}

TEST_F(ProtocolReplayUnitTest, HexDecodeRejectsNonHex) {
	EXPECT_THROW(HexDecode("09zz"), spades::Exception);
}

// ---------------------------------------------------------------------------
// WeaponName / ToolName: map codec ints to schema enums; throw out-of-range.
// ---------------------------------------------------------------------------

TEST_F(ProtocolReplayUnitTest, WeaponNameMapsAndRejects) {
	EXPECT_EQ(WeaponName(0), "rifle");
	EXPECT_EQ(WeaponName(1), "smg");
	EXPECT_EQ(WeaponName(2), "shotgun");
	EXPECT_THROW(WeaponName(3), spades::Exception);
}

TEST_F(ProtocolReplayUnitTest, ToolNameMapsAndRejects) {
	EXPECT_EQ(ToolName(0), "spade");
	EXPECT_EQ(ToolName(1), "block");
	EXPECT_EQ(ToolName(2), "weapon");
	EXPECT_EQ(ToolName(3), "grenade");
	EXPECT_THROW(ToolName(4), spades::Exception);
}

// ---------------------------------------------------------------------------
// Fold: StateData(CTF, playerId=0) → gameMode "ctf", localPlayerIndex 0.
// ---------------------------------------------------------------------------

TEST_F(ProtocolReplayUnitTest, StateDataSetsModeAndLocalIndex) {
	StateDataPacket sd{};
	sd.playerId = 0;
	sd.mode = 0; // CTF
	sd.teamName[0] = "Blue";
	sd.teamName[1] = "Green";
	sd.ctfCaptureLimit = 10;

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeStateData(sd));

	WorldSnapshot snap = ReplaySnapshot(packets, 3);
	EXPECT_EQ(snap.gameMode, "ctf");
	EXPECT_EQ(snap.localPlayerIndex, 0);
	EXPECT_TRUE(snap.players.empty());
}

TEST_F(ProtocolReplayUnitTest, StateDataTcSetsModeTc) {
	StateDataPacket sd{};
	sd.playerId = 4;
	sd.mode = 1; // TC
	sd.teamName[0] = "Red";
	sd.teamName[1] = "Blue";

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeStateData(sd));

	WorldSnapshot snap = ReplaySnapshot(packets, 3);
	EXPECT_EQ(snap.gameMode, "tc");
	EXPECT_EQ(snap.localPlayerIndex, 4);
}

// ---------------------------------------------------------------------------
// Fold: StateData(CTF, local=0) → ExistingPlayer(id=1, team=0, weapon=0, tool=2)
// → WorldUpdate v3 entry index=1 pos={256,256,40}. Non-local alive player IS
// repositioned (NetClient :516-517). RESEARCH <behavior> case.
// ---------------------------------------------------------------------------

TEST_F(ProtocolReplayUnitTest, ExistingPlayerThenWorldUpdateRepositionsNonLocal) {
	StateDataPacket sd{};
	sd.playerId = 0; // local player index 0
	sd.mode = 0;     // CTF
	sd.teamName[0] = "Blue";
	sd.teamName[1] = "Green";

	ExistingPlayerPacket ep{};
	ep.playerId = 1;
	ep.team = 0;
	ep.weapon = 0; // rifle
	ep.tool = 2;   // weapon
	ep.score = 0;
	ep.color = MakeIntVector3(0, 0, 0);
	ep.name = "Bot";

	// v3 WorldUpdate uses IMPLICIT index (idx = position in the list, codec :636).
	// To target player id 1, the entry must sit at list position 1 — so we send two
	// entries: position 0 (local id 0, filler) and position 1 (id 1, the assertion).
	WorldUpdatePacket wu;
	wu.entries.push_back({0, Vector3{0.f, 0.f, 0.f}, Vector3{0.f, 0.f, 1.f}});       // idx 0 (local)
	wu.entries.push_back({1, Vector3{256.f, 256.f, 40.f}, Vector3{1.f, 0.f, 0.f}});  // idx 1

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeStateData(sd));
	Push(packets, EncodeExistingPlayer(ep));
	Push(packets, EncodeWorldUpdate(wu, 3));

	WorldSnapshot snap = ReplaySnapshot(packets, 3);
	ASSERT_EQ(snap.players.count(1), 1u);
	const PlayerState& p = snap.players.at(1);
	EXPECT_EQ(p.id, 1);
	EXPECT_EQ(p.teamId, 0);
	EXPECT_EQ(p.weaponType, "rifle");
	EXPECT_EQ(p.tool, "weapon");
	EXPECT_TRUE(p.alive);
	EXPECT_POS_NEAR(p.position, (Vector3{256.f, 256.f, 40.f}));
	EXPECT_ORI_NEAR(p.orientation, (Vector3{1.f, 0.f, 0.f}));
}

// ---------------------------------------------------------------------------
// Fold: the LOCAL player is NOT repositioned by WorldUpdate (Pitfall 2), but
// savedPos[idx] is written unconditionally (NetClient :523-524) so a LATER
// ExistingPlayer for that id picks it up via savedPos (Pitfall 1).
// ---------------------------------------------------------------------------

TEST_F(ProtocolReplayUnitTest, WorldUpdateDoesNotRepositionLocalButSavesPos) {
	StateDataPacket sd{};
	sd.playerId = 0; // local index 0
	sd.mode = 0;
	sd.teamName[0] = "Blue";
	sd.teamName[1] = "Green";

	// Create the local player first (CreatePlayer at spawn pos), then WorldUpdate idx 0.
	CreatePlayerPacket cp{};
	cp.playerId = 0;
	cp.weapon = 1; // smg
	cp.team = 0;
	cp.position = Vector3{10.f, 20.f, 30.f};
	cp.name = "Me";

	WorldUpdatePacket wu;
	wu.entries.push_back({0, Vector3{999.f, 888.f, 77.f}, Vector3{0.f, 1.f, 0.f}});

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeStateData(sd));
	Push(packets, EncodeCreatePlayer(cp));
	Push(packets, EncodeWorldUpdate(wu, 3));

	WorldSnapshot snap = ReplaySnapshot(packets, 3);
	ASSERT_EQ(snap.players.count(0), 1u);
	const PlayerState& p = snap.players.at(0);
	// Local player keeps the CreatePlayer pos.z-=2.4 position; WorldUpdate did NOT move it.
	EXPECT_POS_NEAR(p.position, (Vector3{10.f, 20.f, 30.f - 2.4f}));
	EXPECT_EQ(p.weaponType, "smg");
}

// ---------------------------------------------------------------------------
// Fold: ExistingPlayer position comes from savedPos (set by a PRIOR WorldUpdate),
// NOT the packet (Pitfall 1). WorldUpdate before ExistingPlayer: the player does
// not exist yet so no reposition, but savedPos[id] is written, and the subsequent
// ExistingPlayer picks it up.
// ---------------------------------------------------------------------------

TEST_F(ProtocolReplayUnitTest, ExistingPlayerPositionFromSavedPos) {
	StateDataPacket sd{};
	sd.playerId = 0;
	sd.mode = 0;
	sd.teamName[0] = "Blue";
	sd.teamName[1] = "Green";

	// v3 implicit index: to write savedPos[2] the entry must be at list position 2.
	WorldUpdatePacket wu;
	wu.entries.push_back({0, Vector3{0.f, 0.f, 0.f}, Vector3{0.f, 0.f, 1.f}});   // idx 0
	wu.entries.push_back({1, Vector3{0.f, 0.f, 0.f}, Vector3{0.f, 0.f, 1.f}});   // idx 1
	wu.entries.push_back({2, Vector3{64.f, 96.f, 12.f}, Vector3{0.f, 0.f, 1.f}}); // idx 2

	ExistingPlayerPacket ep{};
	ep.playerId = 2;
	ep.team = 1;
	ep.weapon = 2; // shotgun
	ep.tool = 0;   // spade
	ep.color = MakeIntVector3(0, 0, 0);
	ep.name = "Late";

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeStateData(sd));
	Push(packets, EncodeWorldUpdate(wu, 3));   // writes savedPos[2], player absent → no reposition
	Push(packets, EncodeExistingPlayer(ep));   // picks up savedPos[2]

	WorldSnapshot snap = ReplaySnapshot(packets, 3);
	ASSERT_EQ(snap.players.count(2), 1u);
	const PlayerState& p = snap.players.at(2);
	EXPECT_EQ(p.weaponType, "shotgun");
	EXPECT_EQ(p.tool, "spade");
	EXPECT_POS_NEAR(p.position, (Vector3{64.f, 96.f, 12.f}));
}

// ---------------------------------------------------------------------------
// Fold: CreatePlayer applies pos.z -= 2.4F (NetClient :718) in the fold.
// ---------------------------------------------------------------------------

TEST_F(ProtocolReplayUnitTest, CreatePlayerAppliesSpawnHeightAdjust) {
	CreatePlayerPacket cp{};
	cp.playerId = 5;
	cp.weapon = 0; // rifle
	cp.team = 1;
	cp.position = Vector3{128.f, 64.f, 50.f};
	cp.name = "Spawn";

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeCreatePlayer(cp));

	WorldSnapshot snap = ReplaySnapshot(packets, 4);
	ASSERT_EQ(snap.players.count(5), 1u);
	const PlayerState& p = snap.players.at(5);
	EXPECT_POS_NEAR(p.position, (Vector3{128.f, 64.f, 50.f - 2.4f}));
	EXPECT_EQ(p.tool, "weapon"); // A4 fold convention (CreatePlayer carries no tool)
	EXPECT_TRUE(p.alive);
}

// ---------------------------------------------------------------------------
// Fold: ExtensionInfo records ALL advertised entries (A3), not a filtered subset.
// ---------------------------------------------------------------------------

TEST_F(ProtocolReplayUnitTest, ExtensionInfoRecordsAllAdvertised) {
	ExtensionInfoPacket ext;
	ext.extensions = {{0, 1}, {192, 1}, {250, 7}}; // 250 is NOT a client-implemented id

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeExtensionInfo(ext));

	WorldSnapshot snap = ReplaySnapshot(packets, 3);
	ASSERT_EQ(snap.extensions.size(), 3u);
	EXPECT_EQ(snap.extensions.at(0), 1);
	EXPECT_EQ(snap.extensions.at(192), 1);
	EXPECT_EQ(snap.extensions.at(250), 7); // recorded despite not being implemented (A3)
}

// ---------------------------------------------------------------------------
// WorldUpdate v4: sparse explicit indices survive and reposition the right player.
// ---------------------------------------------------------------------------

TEST_F(ProtocolReplayUnitTest, WorldUpdateV4SparseIndices) {
	StateDataPacket sd{};
	sd.playerId = 0;
	sd.mode = 0;
	sd.teamName[0] = "Blue";
	sd.teamName[1] = "Green";

	ExistingPlayerPacket ep{};
	ep.playerId = 5;
	ep.team = 0;
	ep.weapon = 1; // smg
	ep.tool = 1;   // block
	ep.color = MakeIntVector3(0, 0, 0);
	ep.name = "Sparse";

	WorldUpdatePacket wu;
	wu.entries.push_back({5, Vector3{42.f, 43.f, 44.f}, Vector3{0.f, 1.f, 0.f}});

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeStateData(sd));
	Push(packets, EncodeExistingPlayer(ep));
	Push(packets, EncodeWorldUpdate(wu, 4)); // v4: leading per-entry index byte

	WorldSnapshot snap = ReplaySnapshot(packets, 4);
	ASSERT_EQ(snap.players.count(5), 1u);
	EXPECT_POS_NEAR(snap.players.at(5).position, (Vector3{42.f, 43.f, 44.f}));
}

// ---------------------------------------------------------------------------
// ToJson: schema-shape — tick + players[] with every required PlayerSnapshot
// field, players ascending by id, game_mode present when set.
// ---------------------------------------------------------------------------

TEST_F(ProtocolReplayUnitTest, ToJsonEmitsSchemaShapeAndSortedPlayers) {
	StateDataPacket sd{};
	sd.playerId = 0;
	sd.mode = 0; // CTF
	sd.teamName[0] = "Blue";
	sd.teamName[1] = "Green";

	// Insert players out of id order to prove ToJson sorts ascending.
	CreatePlayerPacket cp3{};
	cp3.playerId = 3;
	cp3.weapon = 2; // shotgun
	cp3.team = 1;
	cp3.position = Vector3{1.f, 2.f, 3.f};
	cp3.name = "Three";

	CreatePlayerPacket cp1{};
	cp1.playerId = 1;
	cp1.weapon = 0; // rifle
	cp1.team = 0;
	cp1.position = Vector3{4.f, 5.f, 6.f};
	cp1.name = "One";

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeStateData(sd));
	Push(packets, EncodeCreatePlayer(cp3));
	Push(packets, EncodeCreatePlayer(cp1));

	WorldSnapshot snap = ReplaySnapshot(packets, 3);
	nlohmann::json j = snap.ToJson();

	// Top-level world_snapshot.expected shape.
	ASSERT_TRUE(j.contains("tick"));
	EXPECT_EQ(j["tick"].get<int>(), 0);
	ASSERT_TRUE(j.contains("players"));
	ASSERT_TRUE(j["players"].is_array());
	ASSERT_EQ(j["players"].size(), 2u);

	// game_mode present and well-formed.
	ASSERT_TRUE(j.contains("game_mode"));
	EXPECT_EQ(j["game_mode"]["mode"].get<std::string>(), "ctf");

	// Players sorted ascending by id (1 before 3) — Pitfall 5.
	EXPECT_EQ(j["players"][0]["id"].get<int>(), 1);
	EXPECT_EQ(j["players"][1]["id"].get<int>(), 3);

	// Every required PlayerSnapshot field present on each entry.
	for (const auto& pj : j["players"]) {
		EXPECT_TRUE(pj.contains("id"));
		EXPECT_TRUE(pj.contains("alive"));
		EXPECT_TRUE(pj.contains("position"));
		EXPECT_TRUE(pj.contains("velocity"));
		EXPECT_TRUE(pj.contains("orientation"));
		EXPECT_TRUE(pj.contains("health"));
		EXPECT_TRUE(pj.contains("tool"));
		EXPECT_TRUE(pj.contains("weapon_type"));
		// Vec3 sub-shape.
		for (const char* axis : {"x", "y", "z"}) {
			EXPECT_TRUE(pj["position"].contains(axis));
			EXPECT_TRUE(pj["velocity"].contains(axis));
			EXPECT_TRUE(pj["orientation"].contains(axis));
		}
		// A1 fold constants.
		EXPECT_EQ(pj["health"].get<int>(), 100);
		EXPECT_EQ(pj["velocity"]["x"].get<double>(), 0.0);
		EXPECT_EQ(pj["velocity"]["y"].get<double>(), 0.0);
		EXPECT_EQ(pj["velocity"]["z"].get<double>(), 0.0);
	}
}

// ToJson omits game_mode when no StateData set it (GameModeSnapshot requires "mode";
// emitting an empty object would be schema-invalid).
TEST_F(ProtocolReplayUnitTest, ToJsonOmitsGameModeWhenUnset) {
	CreatePlayerPacket cp{};
	cp.playerId = 0;
	cp.weapon = 0;
	cp.team = 0;
	cp.position = Vector3{0.f, 0.f, 0.f};
	cp.name = "Solo";

	std::vector<std::vector<char>> packets;
	Push(packets, EncodeCreatePlayer(cp));

	WorldSnapshot snap = ReplaySnapshot(packets, 3);
	nlohmann::json j = snap.ToJson();
	EXPECT_FALSE(j.contains("game_mode"));
	ASSERT_TRUE(j.contains("players"));
	EXPECT_EQ(j["players"].size(), 1u);
}
