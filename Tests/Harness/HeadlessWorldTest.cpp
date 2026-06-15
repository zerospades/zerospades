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
#include <memory>

#include <gtest/gtest.h>

#include <Client/GameConstants.h>
#include <Client/GameMap.h>
#include <Client/Player.h>
#include <Client/World.h>

#include "HeadlessWorld.h"
#include "MakeFlatMap.h"
#include "SettingsGuard.h"

namespace {

	// Reinterpret a float's bits as a uint32 without UB.
	static uint32_t F2U(float f) {
		uint32_t u;
		std::memcpy(&u, &f, sizeof(u));
		return u;
	}

	// FNV-1a fold over IsSolid for every voxel in the 512x512x64 map.
	static uint64_t MapContentHash(const spades::client::GameMap& map) {
		uint64_t h = 0xcbf29ce484222325ULL; // FNV-1a offset basis
		for (int y = 0; y < 512; ++y) {
			for (int x = 0; x < 512; ++x) {
				for (int z = 0; z < 64; ++z) {
					uint8_t solid = map.IsSolid(x, y, z) ? 1 : 0;
					h ^= solid;
					h *= 1099511628211ULL; // FNV prime
				}
			}
		}
		return h;
	}

} // namespace

class HeadlessWorldTest : public spades::tests::HeadlessTest {};

// ---------------------------------------------------------------------------
// Test 1: HeadlessWorld constructs and destructs without crash or assertion.
// ---------------------------------------------------------------------------
TEST_F(HeadlessWorldTest, Construct) {
	auto vxl = spades::tests::MakeFlatMapBytes();
	spades::tests::HeadlessWorld hw(42, vxl);
	spades::client::World& w = hw.GetWorld();
	EXPECT_GE(w.GetTime(), 0.0F);
}

// ---------------------------------------------------------------------------
// Test 2: After N ticks, world.GetTime() == N * FIXED_DT (within 1e-4).
// ---------------------------------------------------------------------------
TEST_F(HeadlessWorldTest, AdvanceFixedDt) {
	using spades::tests::FIXED_DT;
	auto vxl = spades::tests::MakeFlatMapBytes();

	{
		spades::tests::HeadlessWorld hw(42, vxl);
		hw.Advance(1);
		EXPECT_NEAR(hw.GetWorld().GetTime(), 1.0F * FIXED_DT, 1e-4F);
	}
	{
		spades::tests::HeadlessWorld hw(42, vxl);
		hw.Advance(60);
		EXPECT_NEAR(hw.GetWorld().GetTime(), 60.0F * FIXED_DT, 1e-4F);
	}
}

// ---------------------------------------------------------------------------
// Test 3: DeterminismSmoke1000Ticks
//
// Two independent HeadlessWorld instances with seed 42, each with one Player
// at (256, 256, 32) — above flat ground (z=62) so gravity drives non-trivial
// physics evolution.  After 1000 ticks: per-player position/velocity/
// orientation (front vector) and map content hash must be BITWISE equal.
// Also checks health, tool, world time, and listener counters.
//
// All comparisons use EXPECT_EQ on raw float bits (F2U) — NOT EXPECT_NEAR.
// Same binary + same seed = exact bitwise determinism (CONTEXT.md decision).
// ---------------------------------------------------------------------------
TEST_F(HeadlessWorldTest, DeterminismSmoke1000Ticks) {
	using namespace spades;
	using namespace spades::tests;
	using namespace spades::client;

	// Helper: build a HeadlessWorld seeded at `seed`, add one alive Player at
	// position (256, 256, 32) with zero initial velocity, team 0, RIFLE weapon.
	// HeadlessWorld ctor reseeds the RNG internally; we additionally reseed
	// before construction to guarantee order-independence (T-04-02).
	auto makeWorld = [](std::uint64_t seed) {
		auto vxl = MakeFlatMapBytes();
		SeedLocalRNG(seed, seed ^ 0x9e3779b97f4a7c15ULL);
		auto hw = std::make_unique<HeadlessWorld>(seed, vxl);

		// Construct and register Player 0 (team 0, rifle).
		auto player = std::make_unique<Player>(hw->GetWorld(), 0,
		                                       WeaponType::RIFLE_WEAPON, 0);
		// Place above flat ground so gravity evolves state over 1000 ticks.
		player->SetPosition(Vector3{256.0F, 256.0F, 32.0F});
		player->SetVelocity(Vector3{0.0F, 0.0F, 0.0F});
		// Orient forward along +X axis.
		player->SetOrientation(Vector3{1.0F, 0.0F, 0.0F});

		hw->GetWorld().SetPlayer(0, std::move(player));
		hw->GetWorld().SetLocalPlayerIndex(0);

		return hw;
	};

	// --- Run A ---
	SeedLocalRNG(42, 42 ^ 0x9e3779b97f4a7c15ULL);
	auto hwA = makeWorld(42);
	hwA->Advance(1000);

	auto optA = hwA->GetWorld().GetPlayer(0);
	ASSERT_TRUE(static_cast<bool>(optA)) << "Player 0 not present in run A";
	Player& pA = *optA;

	// --- Run B (independent instance, identical seed) ---
	SeedLocalRNG(42, 42 ^ 0x9e3779b97f4a7c15ULL);
	auto hwB = makeWorld(42);
	hwB->Advance(1000);

	auto optB = hwB->GetWorld().GetPlayer(0);
	ASSERT_TRUE(static_cast<bool>(optB)) << "Player 0 not present in run B";
	Player& pB = *optB;

	// --- Bitwise-equal world time ---
	EXPECT_EQ(hwA->GetWorld().GetTime(), hwB->GetWorld().GetTime())
	    << "World time diverged";

	// --- Bitwise-equal per-player position (raw float bits) ---
	auto posA = pA.GetPosition();
	auto posB = pB.GetPosition();
	EXPECT_EQ(F2U(posA.x), F2U(posB.x)) << "Player position.x diverged";
	EXPECT_EQ(F2U(posA.y), F2U(posB.y)) << "Player position.y diverged";
	EXPECT_EQ(F2U(posA.z), F2U(posB.z)) << "Player position.z diverged";

	// --- Bitwise-equal per-player velocity (raw float bits) ---
	auto velA = pA.GetVelocity();
	auto velB = pB.GetVelocity();
	EXPECT_EQ(F2U(velA.x), F2U(velB.x)) << "Player velocity.x diverged";
	EXPECT_EQ(F2U(velA.y), F2U(velB.y)) << "Player velocity.y diverged";
	EXPECT_EQ(F2U(velA.z), F2U(velB.z)) << "Player velocity.z diverged";

	// --- Bitwise-equal per-player orientation (front vector, raw float bits) ---
	auto fwdA = pA.GetFront();
	auto fwdB = pB.GetFront();
	EXPECT_EQ(F2U(fwdA.x), F2U(fwdB.x)) << "Player orientation.x diverged";
	EXPECT_EQ(F2U(fwdA.y), F2U(fwdB.y)) << "Player orientation.y diverged";
	EXPECT_EQ(F2U(fwdA.z), F2U(fwdB.z)) << "Player orientation.z diverged";

	// --- Bitwise-equal health and tool (int/enum) ---
	EXPECT_EQ(pA.GetHealth(), pB.GetHealth()) << "Player health diverged";
	EXPECT_EQ(static_cast<int>(pA.GetTool()), static_cast<int>(pB.GetTool()))
	    << "Player tool diverged";

	// --- Bitwise-equal map content hash ---
	uint64_t hashA = MapContentHash(*hwA->GetWorld().GetMap());
	uint64_t hashB = MapContentHash(*hwB->GetWorld().GetMap());
	EXPECT_EQ(hashA, hashB) << "Map content hash diverged after 1000 ticks";

	// --- Bitwise-equal listener counters ---
	EXPECT_EQ(hwA->GetListener().playerFiredCount, hwB->GetListener().playerFiredCount)
	    << "playerFiredCount diverged";
	EXPECT_EQ(hwA->GetListener().grenadeExplodedCount,
	          hwB->GetListener().grenadeExplodedCount)
	    << "grenadeExplodedCount diverged";
	EXPECT_EQ(hwA->GetListener().killCount, hwB->GetListener().killCount)
	    << "killCount diverged";
}
