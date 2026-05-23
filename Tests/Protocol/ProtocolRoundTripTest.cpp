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

// Per-packet round-trip TESTs added in Plans 02/03
