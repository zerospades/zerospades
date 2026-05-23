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

#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include <Client/ProtocolCodec.h>

#include "SettingsGuard.h"

using namespace spades;
using namespace spades::client;

namespace {

	// Reinterpret a float's bits as a uint32 without UB. Round-trip asserts use
	// EXPECT_EQ(F2U(a), F2U(b)) for byte-exact float comparison (EXPECT_FLOAT_EQ is
	// banned by the CTest lint; the round-trip is over identical bits, not arithmetic).
	static uint32_t F2U(float f) {
		uint32_t u;
		std::memcpy(&u, &f, sizeof(u));
		return u;
	}

	// Feed a writer's encoded bytes (including the type tag at [0]) back into a reader.
	static NetPacketReader ToReader(NetPacketWriter& w) { return NetPacketReader(w.GetData()); }

} // namespace

// Lightweight fixture: only pins cg_unicode="1" (no World needed for byte round-trips).
class ProtocolRoundTripTest : public ::testing::Test {
protected:
	spades::tests::SettingsGuard guard_;
};

// Guards D-03: the wire ordinals must never drift. 0..34 contiguous, the two dual
// ordinals (5, 31) alias, and the sparse high values (60, 64) are preserved.
TEST_F(ProtocolRoundTripTest, AllPacketTypeOrdinalsStable) {
	EXPECT_EQ(PacketTypePositionData, 0);
	EXPECT_EQ(PacketTypeOrientationData, 1);
	EXPECT_EQ(PacketTypeWorldUpdate, 2);
	EXPECT_EQ(PacketTypeInputData, 3);
	EXPECT_EQ(PacketTypeWeaponInput, 4);
	EXPECT_EQ(PacketTypeHitPacket, 5);
	EXPECT_EQ(PacketTypeSetHP, 5);
	EXPECT_EQ(PacketTypeGrenadePacket, 6);
	EXPECT_EQ(PacketTypeSetTool, 7);
	EXPECT_EQ(PacketTypeSetColour, 8);
	EXPECT_EQ(PacketTypeExistingPlayer, 9);
	EXPECT_EQ(PacketTypeShortPlayerData, 10);
	EXPECT_EQ(PacketTypeMoveObject, 11);
	EXPECT_EQ(PacketTypeCreatePlayer, 12);
	EXPECT_EQ(PacketTypeBlockAction, 13);
	EXPECT_EQ(PacketTypeBlockLine, 14);
	EXPECT_EQ(PacketTypeStateData, 15);
	EXPECT_EQ(PacketTypeKillAction, 16);
	EXPECT_EQ(PacketTypeChatMessage, 17);
	EXPECT_EQ(PacketTypeMapStart, 18);
	EXPECT_EQ(PacketTypeMapChunk, 19);
	EXPECT_EQ(PacketTypePlayerLeft, 20);
	EXPECT_EQ(PacketTypeTerritoryCapture, 21);
	EXPECT_EQ(PacketTypeProgressBar, 22);
	EXPECT_EQ(PacketTypeIntelCapture, 23);
	EXPECT_EQ(PacketTypeIntelPickup, 24);
	EXPECT_EQ(PacketTypeIntelDrop, 25);
	EXPECT_EQ(PacketTypeRestock, 26);
	EXPECT_EQ(PacketTypeFogColour, 27);
	EXPECT_EQ(PacketTypeWeaponReload, 28);
	EXPECT_EQ(PacketTypeChangeTeam, 29);
	EXPECT_EQ(PacketTypeChangeWeapon, 30);
	EXPECT_EQ(PacketTypeMapCached, 31);
	EXPECT_EQ(PacketTypeHandShakeInit, 31);
	EXPECT_EQ(PacketTypeHandShakeReturn, 32);
	EXPECT_EQ(PacketTypeVersionGet, 33);
	EXPECT_EQ(PacketTypeVersionSend, 34);
	EXPECT_EQ(PacketTypeExtensionInfo, 60);
	EXPECT_EQ(PacketTypePlayerProperties, 64);

	// Dual-ordinal aliases must remain equal (same wire byte, two packet shapes).
	EXPECT_EQ(PacketTypeHitPacket, PacketTypeSetHP);
	EXPECT_EQ(PacketTypeMapCached, PacketTypeHandShakeInit);
}

// ===========================================================================
// Plan 02 Task 1: simple/fixed-width + dual-ordinal packet round-trips.
// Pattern: build struct (distinctive non-default values) -> Encode -> ToReader
// -> ASSERT_EQ(GetType()) -> Decode -> EXPECT_EQ per field (F2U for floats) ->
// EXPECT_EQ(GetNumRemainingBytes(), 0u).
// ===========================================================================

TEST_F(ProtocolRoundTripTest, PositionData) {
	PositionDataPacket in{Vector3{1.5f, -2.5f, 3.5f}};
	NetPacketWriter w = EncodePositionData(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypePositionData);
	PositionDataPacket out = DecodePositionData(r);
	EXPECT_EQ(F2U(out.position.x), F2U(in.position.x));
	EXPECT_EQ(F2U(out.position.y), F2U(in.position.y));
	EXPECT_EQ(F2U(out.position.z), F2U(in.position.z));
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, OrientationData) {
	OrientationDataPacket in{Vector3{-0.5f, 0.25f, -0.75f}};
	NetPacketWriter w = EncodeOrientationData(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeOrientationData);
	OrientationDataPacket out = DecodeOrientationData(r);
	EXPECT_EQ(F2U(out.orientation.x), F2U(in.orientation.x));
	EXPECT_EQ(F2U(out.orientation.y), F2U(in.orientation.y));
	EXPECT_EQ(F2U(out.orientation.z), F2U(in.orientation.z));
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, InputData) {
	InputDataPacket in{17, 0xA5};
	NetPacketWriter w = EncodeInputData(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeInputData);
	InputDataPacket out = DecodeInputData(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.bits, in.bits);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, WeaponInput) {
	WeaponInputPacket in{23, 0x03};
	NetPacketWriter w = EncodeWeaponInput(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeWeaponInput);
	WeaponInputPacket out = DecodeWeaponInput(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.bits, in.bits);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

// DUAL ORDINAL 5 — HitPacket (C2S) and SetHP (S2C) share PacketType 5.
TEST_F(ProtocolRoundTripTest, HitPacket) {
	HitPacketPacket in{42, 4}; // targetId, hitType=melee
	NetPacketWriter w = EncodeHitPacket(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeHitPacket);
	HitPacketPacket out = DecodeHitPacket(r);
	EXPECT_EQ(out.targetId, in.targetId);
	EXPECT_EQ(out.hitType, in.hitType);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, SetHP) {
	SetHPPacket in{73, 1, Vector3{10.5f, -20.5f, 30.5f}}; // hp, type=weapon, source
	NetPacketWriter w = EncodeSetHP(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeSetHP);
	SetHPPacket out = DecodeSetHP(r);
	EXPECT_EQ(out.hp, in.hp);
	EXPECT_EQ(out.type, in.type);
	EXPECT_EQ(F2U(out.source.x), F2U(in.source.x));
	EXPECT_EQ(F2U(out.source.y), F2U(in.source.y));
	EXPECT_EQ(F2U(out.source.z), F2U(in.source.z));
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, Grenade) {
	GrenadePacket in{7, 2.5f, Vector3{1.f, 2.f, 3.f}, Vector3{-4.f, 5.f, -6.f}};
	NetPacketWriter w = EncodeGrenade(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeGrenadePacket);
	GrenadePacket out = DecodeGrenade(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(F2U(out.fuse), F2U(in.fuse));
	EXPECT_EQ(F2U(out.position.x), F2U(in.position.x));
	EXPECT_EQ(F2U(out.position.y), F2U(in.position.y));
	EXPECT_EQ(F2U(out.position.z), F2U(in.position.z));
	EXPECT_EQ(F2U(out.velocity.x), F2U(in.velocity.x));
	EXPECT_EQ(F2U(out.velocity.y), F2U(in.velocity.y));
	EXPECT_EQ(F2U(out.velocity.z), F2U(in.velocity.z));
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, SetTool) {
	SetToolPacket in{19, 3}; // playerId, tool=grenade
	NetPacketWriter w = EncodeSetTool(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeSetTool);
	SetToolPacket out = DecodeSetTool(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.tool, in.tool);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, SetColour) {
	// Asymmetric color {R=10,G=20,B=30} catches a BGR/RGB swap.
	SetColourPacket in{5, MakeIntVector3(10, 20, 30)};
	NetPacketWriter w = EncodeSetColour(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeSetColour);
	SetColourPacket out = DecodeSetColour(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.color.x, in.color.x); // R
	EXPECT_EQ(out.color.y, in.color.y); // G
	EXPECT_EQ(out.color.z, in.color.z); // B
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

// GOLDEN-BYTE SPOT CHECK #1 (T-3-02): proves the on-wire color bytes are B,G,R
// (not R,G,B). A symmetric Encode/Decode that wrote RGB would still round-trip,
// but would fail this hardcoded byte assert.
TEST_F(ProtocolRoundTripTest, SetColourGoldenBytesBGR) {
	SetColourPacket in{5, MakeIntVector3(10, 20, 30)}; // R=10,G=20,B=30
	NetPacketWriter w = EncodeSetColour(in);
	std::vector<char> bytes = w.GetData();
	// [0]=type tag(8), [1]=playerId(5), [2]=B(30), [3]=G(20), [4]=R(10)
	std::vector<char> expected = {
		(char)PacketTypeSetColour, (char)5, (char)30, (char)20, (char)10};
	EXPECT_EQ(bytes, expected);
}

// [ASSUMED] spec-derived layout — no in-repo decode oracle (RESEARCH A1).
TEST_F(ProtocolRoundTripTest, ShortPlayerData) {
	ShortPlayerDataPacket in{12, 1, 2}; // playerId, team, weapon (spec layout)
	NetPacketWriter w = EncodeShortPlayerData(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeShortPlayerData);
	ShortPlayerDataPacket out = DecodeShortPlayerData(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.team, in.team);
	EXPECT_EQ(out.weapon, in.weapon);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, BlockAction) {
	BlockActionPacket in{31, 2, MakeIntVector3(100, -200, 300)};
	NetPacketWriter w = EncodeBlockAction(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeBlockAction);
	BlockActionPacket out = DecodeBlockAction(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.action, in.action);
	EXPECT_EQ(out.position.x, in.position.x);
	EXPECT_EQ(out.position.y, in.position.y);
	EXPECT_EQ(out.position.z, in.position.z);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, BlockLine) {
	BlockLinePacket in{8, MakeIntVector3(1, 2, 3), MakeIntVector3(40, 50, 60)};
	NetPacketWriter w = EncodeBlockLine(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeBlockLine);
	BlockLinePacket out = DecodeBlockLine(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.start.x, in.start.x);
	EXPECT_EQ(out.start.y, in.start.y);
	EXPECT_EQ(out.start.z, in.start.z);
	EXPECT_EQ(out.end.x, in.end.x);
	EXPECT_EQ(out.end.y, in.end.y);
	EXPECT_EQ(out.end.z, in.end.z);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, KillAction) {
	KillActionPacket in{3, 9, 6, 15}; // victimId, killerId, killType=classChange, respawn
	NetPacketWriter w = EncodeKillAction(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeKillAction);
	KillActionPacket out = DecodeKillAction(r);
	EXPECT_EQ(out.victimId, in.victimId);
	EXPECT_EQ(out.killerId, in.killerId);
	EXPECT_EQ(out.killType, in.killType);
	EXPECT_EQ(out.respawnTime, in.respawnTime);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, WeaponReload) {
	WeaponReloadPacket in{6, 11, 22}; // playerId, clip, reserve
	NetPacketWriter w = EncodeWeaponReload(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeWeaponReload);
	WeaponReloadPacket out = DecodeWeaponReload(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.clip, in.clip);
	EXPECT_EQ(out.reserve, in.reserve);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, ChangeTeam) {
	ChangeTeamPacket in{14, 2}; // playerId, team
	NetPacketWriter w = EncodeChangeTeam(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeChangeTeam);
	ChangeTeamPacket out = DecodeChangeTeam(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.team, in.team);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, ChangeWeapon) {
	ChangeWeaponPacket in{21, 1}; // playerId, weapon=SMG
	NetPacketWriter w = EncodeChangeWeapon(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeChangeWeapon);
	ChangeWeaponPacket out = DecodeChangeWeapon(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.weapon, in.weapon);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

// DUAL ORDINAL 31 — MapCached (C2S) and HandShakeInit (S2C) share PacketType 31.
TEST_F(ProtocolRoundTripTest, MapCached) {
	MapCachedPacket in{1}; // cached
	NetPacketWriter w = EncodeMapCached(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeMapCached);
	MapCachedPacket out = DecodeMapCached(r);
	EXPECT_EQ(out.cached, in.cached);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, HandShakeInit) {
	HandShakeInitPacket in{0xDEADBEEFu}; // challenge
	NetPacketWriter w = EncodeHandShakeInit(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeHandShakeInit);
	HandShakeInitPacket out = DecodeHandShakeInit(r);
	EXPECT_EQ(out.challenge, in.challenge);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, HandShakeReturn) {
	HandShakeReturnPacket in{0x12345678u}; // challenge
	NetPacketWriter w = EncodeHandShakeReturn(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeHandShakeReturn);
	HandShakeReturnPacket out = DecodeHandShakeReturn(r);
	EXPECT_EQ(out.challenge, in.challenge);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, PlayerProperties) {
	PlayerPropertiesPacket in{1, 2, 100, 50, 3, 8, 40, 250};
	NetPacketWriter w = EncodePlayerProperties(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypePlayerProperties);
	PlayerPropertiesPacket out = DecodePlayerProperties(r);
	EXPECT_EQ(out.subId, in.subId);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.hp, in.hp);
	EXPECT_EQ(out.blocks, in.blocks);
	EXPECT_EQ(out.grenades, in.grenades);
	EXPECT_EQ(out.clip, in.clip);
	EXPECT_EQ(out.reserve, in.reserve);
	EXPECT_EQ(out.score, in.score);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

// ===========================================================================
// Plan 02 Task 2: game-mode-coupled, string, list, signed-field packet round-trips.
// ===========================================================================

TEST_F(ProtocolRoundTripTest, MoveObject) {
	MoveObjectPacket in{2, 1, Vector3{11.f, -22.f, 33.f}}; // type, state, pos
	NetPacketWriter w = EncodeMoveObject(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeMoveObject);
	MoveObjectPacket out = DecodeMoveObject(r);
	EXPECT_EQ(out.type, in.type);
	EXPECT_EQ(out.state, in.state);
	EXPECT_EQ(F2U(out.position.x), F2U(in.position.x));
	EXPECT_EQ(F2U(out.position.y), F2U(in.position.y));
	EXPECT_EQ(F2U(out.position.z), F2U(in.position.z));
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, ChatMessage) {
	// CP437-representable distinctive string (cg_unicode pinned via SettingsGuard).
	ChatMessagePacket in{200, 1, std::string("Hello, ZeroSpades 123!")};
	NetPacketWriter w = EncodeChatMessage(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeChatMessage);
	ChatMessagePacket out = DecodeChatMessage(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.type, in.type);
	EXPECT_EQ(out.message, in.message);
	// NOTE: ReadRemainingString (the verbatim-moved reader) does NOT advance pos on a
	// terminal remaining-read, so GetNumRemainingBytes() is not 0 here. The decoded
	// message equality already proves byte-exactness of the whole tail.
}

TEST_F(ProtocolRoundTripTest, MapStart) {
	// Bytes are version-independent (single uint32); covers both protocol 3 and 4.
	MapStartPacket in{0xCAFEBABEu};
	NetPacketWriter w = EncodeMapStart(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeMapStart);
	MapStartPacket out = DecodeMapStart(r);
	EXPECT_EQ(out.mapSize, in.mapSize);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, MapChunk) {
	// Raw remaining bytes round-trip exactly (no string normalization).
	MapChunkPacket in{std::string("\x01\x02\xFE\xFF\x7F\x80", 6)};
	NetPacketWriter w = EncodeMapChunk(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeMapChunk);
	MapChunkPacket out = DecodeMapChunk(r);
	EXPECT_EQ(out.data, in.data);
	// NOTE: ReadRemainingData does not advance pos (verbatim reader); the data
	// equality proves the full raw byte tail round-tripped exactly.
}

TEST_F(ProtocolRoundTripTest, PlayerLeft) {
	PlayerLeftPacket in{27};
	NetPacketWriter w = EncodePlayerLeft(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypePlayerLeft);
	PlayerLeftPacket out = DecodePlayerLeft(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, TerritoryCapture) {
	TerritoryCapturePacket in{3, 1, 2}; // territoryId, winning, state
	NetPacketWriter w = EncodeTerritoryCapture(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeTerritoryCapture);
	TerritoryCapturePacket out = DecodeTerritoryCapture(r);
	EXPECT_EQ(out.territoryId, in.territoryId);
	EXPECT_EQ(out.winning, in.winning);
	EXPECT_EQ(out.state, in.state);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

// rate is SIGNED — rate=-7 must survive the byte round-trip (catches uint8 mis-typing).
TEST_F(ProtocolRoundTripTest, ProgressBar) {
	ProgressBarPacket in{4, 1, (int8_t)-7, 0.625f};
	NetPacketWriter w = EncodeProgressBar(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeProgressBar);
	ProgressBarPacket out = DecodeProgressBar(r);
	EXPECT_EQ(out.territoryId, in.territoryId);
	EXPECT_EQ(out.capturingTeam, in.capturingTeam);
	EXPECT_EQ(out.rate, in.rate);
	EXPECT_EQ((int)out.rate, -7);
	EXPECT_EQ(F2U(out.progress), F2U(in.progress));
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

// GOLDEN-BYTE SPOT CHECK #2 (T-3-02): proves the signed rate byte is emitted as
// 0xF9 (= -7 reinterpreted as uint8). A round-trip alone would pass even if both
// Encode and Decode wrongly used an unsigned width; this hardcoded byte catches it.
TEST_F(ProtocolRoundTripTest, ProgressBarGoldenBytesSignedRate) {
	ProgressBarPacket in{4, 1, (int8_t)-7, 0.625f};
	NetPacketWriter w = EncodeProgressBar(in);
	std::vector<char> bytes = w.GetData();
	// [0]=type(22), [1]=territoryId(4), [2]=capturingTeam(1), [3]=rate(0xF9), [4..7]=float
	ASSERT_GE(bytes.size(), 4u);
	EXPECT_EQ((uint8_t)bytes[0], (uint8_t)PacketTypeProgressBar);
	EXPECT_EQ((uint8_t)bytes[1], 4u);
	EXPECT_EQ((uint8_t)bytes[2], 1u);
	EXPECT_EQ((uint8_t)bytes[3], 0xF9u); // -7 as uint8
}

TEST_F(ProtocolRoundTripTest, IntelCapture) {
	IntelCapturePacket in{15, 1}; // playerId, winning
	NetPacketWriter w = EncodeIntelCapture(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeIntelCapture);
	IntelCapturePacket out = DecodeIntelCapture(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.winning, in.winning);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, IntelPickup) {
	IntelPickupPacket in{18};
	NetPacketWriter w = EncodeIntelPickup(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeIntelPickup);
	IntelPickupPacket out = DecodeIntelPickup(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, IntelDrop) {
	IntelDropPacket in{9, Vector3{7.5f, -8.5f, 9.5f}};
	NetPacketWriter w = EncodeIntelDrop(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeIntelDrop);
	IntelDropPacket out = DecodeIntelDrop(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(F2U(out.position.x), F2U(in.position.x));
	EXPECT_EQ(F2U(out.position.y), F2U(in.position.y));
	EXPECT_EQ(F2U(out.position.z), F2U(in.position.z));
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, Restock) {
	RestockPacket in{13};
	NetPacketWriter w = EncodeRestock(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeRestock);
	RestockPacket out = DecodeRestock(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

// recv skips alpha then reads BGR; alpha round-trips as the struct field.
TEST_F(ProtocolRoundTripTest, FogColour) {
	FogColourPacket in{128, MakeIntVector3(40, 50, 60)}; // alpha, R=40,G=50,B=60
	NetPacketWriter w = EncodeFogColour(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeFogColour);
	FogColourPacket out = DecodeFogColour(r);
	EXPECT_EQ(out.alpha, in.alpha);
	EXPECT_EQ(out.color.x, in.color.x); // R
	EXPECT_EQ(out.color.y, in.color.y); // G
	EXPECT_EQ(out.color.z, in.color.z); // B
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, VersionGet) {
	// Enhanced variant: list of property ids round-trips with correct count.
	VersionGetPacket in;
	in.propertyIds = {1, 2, 3};
	NetPacketWriter w = EncodeVersionGet(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeVersionGet);
	VersionGetPacket out = DecodeVersionGet(r);
	EXPECT_EQ(out.propertyIds, in.propertyIds);
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

TEST_F(ProtocolRoundTripTest, VersionSend) {
	VersionSendPacket in{(uint8_t)'o', 1, 2, 3, std::string("Linux x86_64")};
	NetPacketWriter w = EncodeVersionSend(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeVersionSend);
	VersionSendPacket out = DecodeVersionSend(r);
	EXPECT_EQ(out.tag, in.tag);
	EXPECT_EQ(out.major, in.major);
	EXPECT_EQ(out.minor, in.minor);
	EXPECT_EQ(out.patch, in.patch);
	EXPECT_EQ(out.osInfo, in.osInfo);
	// NOTE: osInfo via ReadRemainingString does not advance pos (verbatim reader);
	// the osInfo equality proves the trailing string round-tripped exactly.
}

TEST_F(ProtocolRoundTripTest, ExtensionInfo) {
	ExtensionInfoPacket in;
	in.extensions = {{1, 10}, {2, 20}, {3, 30}}; // 3 {id,version} entries
	NetPacketWriter w = EncodeExtensionInfo(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeExtensionInfo);
	ExtensionInfoPacket out = DecodeExtensionInfo(r);
	ASSERT_EQ(out.extensions.size(), in.extensions.size());
	for (size_t i = 0; i < in.extensions.size(); i++) {
		EXPECT_EQ(out.extensions[i].id, in.extensions[i].id);
		EXPECT_EQ(out.extensions[i].version, in.extensions[i].version);
	}
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

// ===========================================================================
// Plan 03 Task 1: the HARD packets — WorldUpdate (version-branched, D-13) and
// StateData (discriminated CTF/TC, D-10).
// ===========================================================================

// WorldUpdate v3 (protocol 0.75): 24 B/entry, index IMPLICIT (idx=i). Encode
// writes NO index byte; Decode assigns index=i by position.
TEST_F(ProtocolRoundTripTest, WorldUpdate_v3) {
	WorldUpdatePacket in;
	in.entries.push_back({0, Vector3{1.5f, -2.5f, 3.5f}, Vector3{0.1f, 0.2f, 0.3f}});
	in.entries.push_back({1, Vector3{-10.f, 20.f, -30.f}, Vector3{-0.4f, 0.5f, -0.6f}});
	in.entries.push_back({2, Vector3{100.25f, -200.5f, 300.75f}, Vector3{0.7f, -0.8f, 0.9f}});
	NetPacketWriter w = EncodeWorldUpdate(in, 3);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeWorldUpdate);
	WorldUpdatePacket out = DecodeWorldUpdate(r, 3);
	ASSERT_EQ(out.entries.size(), 3u);
	for (size_t i = 0; i < in.entries.size(); i++) {
		EXPECT_EQ(out.entries[i].index, (uint8_t)i); // implicit index by position
		EXPECT_EQ(F2U(out.entries[i].position.x), F2U(in.entries[i].position.x));
		EXPECT_EQ(F2U(out.entries[i].position.y), F2U(in.entries[i].position.y));
		EXPECT_EQ(F2U(out.entries[i].position.z), F2U(in.entries[i].position.z));
		EXPECT_EQ(F2U(out.entries[i].front.x), F2U(in.entries[i].front.x));
		EXPECT_EQ(F2U(out.entries[i].front.y), F2U(in.entries[i].front.y));
		EXPECT_EQ(F2U(out.entries[i].front.z), F2U(in.entries[i].front.z));
	}
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

// WorldUpdate v4 (protocol 0.76): 25 B/entry, leading EXPLICIT ReadByte index
// (sparse). Non-sequential indices (5,2,9) must survive the round-trip.
TEST_F(ProtocolRoundTripTest, WorldUpdate_v4) {
	WorldUpdatePacket in;
	in.entries.push_back({5, Vector3{1.5f, -2.5f, 3.5f}, Vector3{0.1f, 0.2f, 0.3f}});
	in.entries.push_back({2, Vector3{-10.f, 20.f, -30.f}, Vector3{-0.4f, 0.5f, -0.6f}});
	in.entries.push_back({9, Vector3{100.25f, -200.5f, 300.75f}, Vector3{0.7f, -0.8f, 0.9f}});
	NetPacketWriter w = EncodeWorldUpdate(in, 4);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeWorldUpdate);
	WorldUpdatePacket out = DecodeWorldUpdate(r, 4);
	ASSERT_EQ(out.entries.size(), 3u);
	const uint8_t expectedIdx[3] = {5, 2, 9};
	for (size_t i = 0; i < in.entries.size(); i++) {
		EXPECT_EQ(out.entries[i].index, expectedIdx[i]); // explicit index survives
		EXPECT_EQ(F2U(out.entries[i].position.x), F2U(in.entries[i].position.x));
		EXPECT_EQ(F2U(out.entries[i].position.y), F2U(in.entries[i].position.y));
		EXPECT_EQ(F2U(out.entries[i].position.z), F2U(in.entries[i].position.z));
		EXPECT_EQ(F2U(out.entries[i].front.x), F2U(in.entries[i].front.x));
		EXPECT_EQ(F2U(out.entries[i].front.y), F2U(in.entries[i].front.y));
		EXPECT_EQ(F2U(out.entries[i].front.z), F2U(in.entries[i].front.z));
	}
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

// GOLDEN-BYTE SPOT CHECK #3 (T-3-04): proves the v4 layout writes the leading
// per-entry index byte at position [1] (right after the type tag at [0]). A v3
// encode (24 B/entry, no index byte) would put the float pos at [1] instead.
TEST_F(ProtocolRoundTripTest, WorldUpdateV4GoldenBytesIndex) {
	WorldUpdatePacket in;
	in.entries.push_back({5, Vector3{1.f, 2.f, 3.f}, Vector3{4.f, 5.f, 6.f}});
	NetPacketWriter w = EncodeWorldUpdate(in, 4);
	std::vector<char> bytes = w.GetData();
	// [0]=type tag(2=WorldUpdate), [1]=leading index byte(5), then 24 B pos+front.
	ASSERT_EQ(bytes.size(), 1u + 25u); // tag + one 25-byte v4 entry
	EXPECT_EQ((uint8_t)bytes[0], (uint8_t)PacketTypeWorldUpdate);
	EXPECT_EQ((uint8_t)bytes[1], 5u); // the per-entry index byte
}

// StateData CTF (mode==0) with BOTH intel flags set: each carried-intel team
// emits carrierId + 11 bytes (in the team-2-first order); basePos for both teams
// must still round-trip, proving the 11-byte skip count is exact (T-3-03).
TEST_F(ProtocolRoundTripTest, StateData_CTF) {
	StateDataPacket in{};
	in.playerId = 7;
	in.fogColor = MakeIntVector3(11, 22, 33);
	in.teamColor[0] = MakeIntVector3(40, 50, 60);
	in.teamColor[1] = MakeIntVector3(70, 80, 90);
	in.teamName[0] = std::string("Blue");
	in.teamName[1] = std::string("Green");
	in.mode = 0; // CTF
	in.ctfTeam1Score = 3;
	in.ctfTeam2Score = 5;
	in.ctfCaptureLimit = 10;
	in.ctfIntelFlags = 0x3; // both teams carry intel
	in.ctfTeam1CarrierId = 12;
	in.ctfTeam2CarrierId = 21;
	in.ctfTeam1BasePos = Vector3{1.5f, -2.5f, 3.5f};
	in.ctfTeam2BasePos = Vector3{-4.5f, 5.5f, -6.5f};
	NetPacketWriter w = EncodeStateData(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeStateData);
	StateDataPacket out = DecodeStateData(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.fogColor.x, in.fogColor.x);
	EXPECT_EQ(out.fogColor.y, in.fogColor.y);
	EXPECT_EQ(out.fogColor.z, in.fogColor.z);
	EXPECT_EQ(out.teamColor[0].x, in.teamColor[0].x);
	EXPECT_EQ(out.teamColor[0].z, in.teamColor[0].z);
	EXPECT_EQ(out.teamColor[1].y, in.teamColor[1].y);
	EXPECT_EQ(out.teamName[0], in.teamName[0]);
	EXPECT_EQ(out.teamName[1], in.teamName[1]);
	EXPECT_EQ(out.mode, in.mode);
	EXPECT_EQ(out.ctfTeam1Score, in.ctfTeam1Score);
	EXPECT_EQ(out.ctfTeam2Score, in.ctfTeam2Score);
	EXPECT_EQ(out.ctfCaptureLimit, in.ctfCaptureLimit);
	EXPECT_EQ(out.ctfIntelFlags, in.ctfIntelFlags);
	EXPECT_EQ(out.ctfTeam1CarrierId, in.ctfTeam1CarrierId);
	EXPECT_EQ(out.ctfTeam2CarrierId, in.ctfTeam2CarrierId);
	// basePos survives the 11-byte carrier-slot skip for BOTH teams (T-3-03).
	EXPECT_EQ(F2U(out.ctfTeam1BasePos.x), F2U(in.ctfTeam1BasePos.x));
	EXPECT_EQ(F2U(out.ctfTeam1BasePos.y), F2U(in.ctfTeam1BasePos.y));
	EXPECT_EQ(F2U(out.ctfTeam1BasePos.z), F2U(in.ctfTeam1BasePos.z));
	EXPECT_EQ(F2U(out.ctfTeam2BasePos.x), F2U(in.ctfTeam2BasePos.x));
	EXPECT_EQ(F2U(out.ctfTeam2BasePos.y), F2U(in.ctfTeam2BasePos.y));
	EXPECT_EQ(F2U(out.ctfTeam2BasePos.z), F2U(in.ctfTeam2BasePos.z));
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

// StateData CTF with NO intel flags: both teams' flagPos (Vector3) are read in
// place of the carrier slots; exercises the else-branches of both conditionals.
TEST_F(ProtocolRoundTripTest, StateData_CTF_NoIntel) {
	StateDataPacket in{};
	in.playerId = 1;
	in.fogColor = MakeIntVector3(1, 2, 3);
	in.teamColor[0] = MakeIntVector3(4, 5, 6);
	in.teamColor[1] = MakeIntVector3(7, 8, 9);
	in.teamName[0] = std::string("A");
	in.teamName[1] = std::string("B");
	in.mode = 0;
	in.ctfTeam1Score = 0;
	in.ctfTeam2Score = 0;
	in.ctfCaptureLimit = 10;
	in.ctfIntelFlags = 0x0; // neither team carries intel -> flagPos read for both
	in.ctfTeam1FlagPos = Vector3{10.5f, -11.5f, 12.5f};
	in.ctfTeam2FlagPos = Vector3{-13.5f, 14.5f, -15.5f};
	in.ctfTeam1BasePos = Vector3{16.f, 17.f, 18.f};
	in.ctfTeam2BasePos = Vector3{-19.f, -20.f, -21.f};
	NetPacketWriter w = EncodeStateData(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeStateData);
	StateDataPacket out = DecodeStateData(r);
	EXPECT_EQ(out.ctfIntelFlags, 0x0);
	EXPECT_EQ(F2U(out.ctfTeam1FlagPos.x), F2U(in.ctfTeam1FlagPos.x));
	EXPECT_EQ(F2U(out.ctfTeam1FlagPos.z), F2U(in.ctfTeam1FlagPos.z));
	EXPECT_EQ(F2U(out.ctfTeam2FlagPos.y), F2U(in.ctfTeam2FlagPos.y));
	EXPECT_EQ(F2U(out.ctfTeam1BasePos.x), F2U(in.ctfTeam1BasePos.x));
	EXPECT_EQ(F2U(out.ctfTeam2BasePos.z), F2U(in.ctfTeam2BasePos.z));
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}

// StateData TC (mode!=0): territoryCount then per-territory [pos(12B), state(1B)].
TEST_F(ProtocolRoundTripTest, StateData_TC) {
	StateDataPacket in{};
	in.playerId = 4;
	in.fogColor = MakeIntVector3(31, 32, 33);
	in.teamColor[0] = MakeIntVector3(34, 35, 36);
	in.teamColor[1] = MakeIntVector3(37, 38, 39);
	in.teamName[0] = std::string("Red");
	in.teamName[1] = std::string("Blue");
	in.mode = 1; // TC
	in.tcTerritories.push_back({Vector3{1.f, 2.f, 3.f}, 0});
	in.tcTerritories.push_back({Vector3{-4.f, 5.f, -6.f}, 1});
	in.tcTerritories.push_back({Vector3{7.5f, -8.5f, 9.5f}, 2});
	NetPacketWriter w = EncodeStateData(in);
	NetPacketReader r = ToReader(w);
	ASSERT_EQ(r.GetType(), PacketTypeStateData);
	StateDataPacket out = DecodeStateData(r);
	EXPECT_EQ(out.playerId, in.playerId);
	EXPECT_EQ(out.mode, in.mode);
	EXPECT_EQ(out.teamName[0], in.teamName[0]);
	EXPECT_EQ(out.teamName[1], in.teamName[1]);
	ASSERT_EQ(out.tcTerritories.size(), in.tcTerritories.size());
	for (size_t i = 0; i < in.tcTerritories.size(); i++) {
		EXPECT_EQ(F2U(out.tcTerritories[i].pos.x), F2U(in.tcTerritories[i].pos.x));
		EXPECT_EQ(F2U(out.tcTerritories[i].pos.y), F2U(in.tcTerritories[i].pos.y));
		EXPECT_EQ(F2U(out.tcTerritories[i].pos.z), F2U(in.tcTerritories[i].pos.z));
		EXPECT_EQ(out.tcTerritories[i].state, in.tcTerritories[i].state);
	}
	EXPECT_EQ(r.GetNumRemainingBytes(), 0u);
}
