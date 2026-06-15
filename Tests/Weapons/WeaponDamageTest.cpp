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

// WEAP-01: weapon damage / spread / pellet / clip / stock / reload constants.
//
// Pattern: generate-then-freeze (Phases 4-6 established).
//   - DISABLED_WeaponDamageGenerate.* / DISABLED_WeaponModifierGenerate.* emit
//     the per-variant constant tables from the C++ oracle (the six Weapon
//     subclasses RifleWeapon3/4, SMGWeapon3/4, ShotgunWeapon3/4). Run manually
//     once with --gtest_also_run_disabled_tests, commit the frozen fixtures,
//     then never again.
//   - WeaponTestBase.* replay tests load the frozen fixture, reconstruct the
//     same weapon, and assert match: integers (damage rows, pellets, clip,
//     stock) via EXPECT_EQ (catches table drift exactly), float scalars
//     (spread/delay_s/reload_s) via EXPECT_NEAR (POSITION_TOL). All expected
//     values are read FROM the JSON — no oracle value is hardcoded in a replay
//     path (portability, SCHE-06).
//
// Oracle quirks characterized here (RESEARCH Pitfalls 1/2):
//   - GetDamage handles 5 of the 6 HitType enum values. HitTypeMelee has no
//     case (default: SPAssert(false); return 0) — it is NOT asserted with a
//     damage value; a "melee_note" string documents the unhandled-input quirk
//     so a Rust port reproduces it (assert in debug, return 0 in release).
//   - RifleWeapon4's source comment calls its table "the 0.75 damage values",
//     but the client returns Head=250/Torso=60/etc. for v076. We freeze exactly
//     what GetDamage returns; the fixture characterizes the client oracle.
//
// v076 construction (RESEARCH OQ-3): the pure-constant lookups read no
// Player/World state, so we build a v075 HeadlessWorld + Player once and select
// each variant via Weapon::CreateWeapon(type, player, GameProperties(version)).
// The v076 GameProperties picks the RifleWeapon4/SMGWeapon4/ShotgunWeapon4
// subclasses; their constant getters are independent of the owning world.

#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <Client/GameConstants.h>
#include <Client/GameProperties.h>
#include <Client/Player.h>
#include <Client/Weapon.h>
#include <Client/World.h>
#include <Core/Math.h>

#include "HeadlessWorld.h"
#include "MakeFlatMap.h"
#include "SettingsGuard.h"
#include "ToleranceMatchers.h"
#include "WeaponTestBase.h"

using namespace spades;
using namespace spades::tests;
using namespace spades::client;

namespace {

	// A weapon owned by a v075 HeadlessWorld Player. For v076 variants the
	// GameProperties(v076) passed to CreateWeapon selects the *Weapon4 subclass;
	// the constant getters do not read the owning World's protocol, so reusing
	// the v075 world/player as the inert owner is sound (RESEARCH OQ-3).
	struct WeaponHarness {
		HeadlessWorld hw;
		client::Player* player;
		std::unique_ptr<client::Weapon> weapon;

		WeaponHarness(WeaponType type, ProtocolVersion version)
		    : hw(42, MakeFlatMapBytes()) {
			auto p = std::make_unique<client::Player>(hw.GetWorld(), 0, type, 0);
			hw.GetWorld().SetPlayer(0, std::move(p));
			auto opt = hw.GetWorld().GetPlayer(0);
			player = opt ? &*opt : nullptr;
			client::GameProperties props(version);
			weapon.reset(client::Weapon::CreateWeapon(type, *player, props));
		}

		client::Weapon& W() { return *weapon; }
	};

	const char* WeaponName(WeaponType t) {
		switch (t) {
			case RIFLE_WEAPON: return "rifle";
			case SMG_WEAPON: return "smg";
			case SHOTGUN_WEAPON: return "shotgun";
			default: return "?";
		}
	}

	// Build the value_lookup payload from the oracle's pure-constant getters.
	// Damage covers the 5 VALID HitTypes only (Melee unhandled — Pitfall 1).
	nlohmann::json BuildWeaponValue(client::Weapon& w, WeaponType type, const std::string& protocol) {
		return nlohmann::json{
		    {"weapon", WeaponName(type)},
		    {"protocol", protocol},
		    {"damage",
		     {{"head", w.GetDamage(HitTypeHead)},
		      {"torso", w.GetDamage(HitTypeTorso)},
		      {"arms", w.GetDamage(HitTypeArms)},
		      {"legs", w.GetDamage(HitTypeLegs)},
		      {"block", w.GetDamage(HitTypeBlock)}}},
		    {"spread", static_cast<double>(w.GetSpread())},
		    {"pellets", w.GetPelletSize()},
		    {"delay_s", static_cast<double>(w.GetDelay())},
		    {"clip", w.GetClipSize()},
		    {"stock", w.GetMaxStock()},
		    {"reload_s", static_cast<double>(w.GetReloadTime())},
		    {"reload_slow", w.IsReloadSlow()},
		    // HitTypeMelee is intentionally absent (GetDamage has no Melee case;
		    // default: SPAssert(false); return 0). Document, do not assert.
		    {"melee_note",
		     "HitTypeMelee is unhandled by GetDamage (default: SPAssert(false); "
		     "return 0). Melee damage is server-authoritative; the client never "
		     "queries it. A port should assert in debug / return 0 in release."},
		};
	}

	// Reconstruct a weapon and assert each frozen field. Integers exact, floats
	// near POSITION_TOL. Reads everything from the fixture's expected.value.
	void ReplayWeaponFixture(const std::string& file, WeaponType type, ProtocolVersion version) {
		nlohmann::json j = LoadWeaponFixtureJson(file);
		const auto& val = j.at("expected").at("value");

		WeaponHarness h(type, version);
		client::Weapon& w = h.W();

		EXPECT_EQ(w.GetDamage(HitTypeHead), val.at("damage").at("head").get<int>());
		EXPECT_EQ(w.GetDamage(HitTypeTorso), val.at("damage").at("torso").get<int>());
		EXPECT_EQ(w.GetDamage(HitTypeArms), val.at("damage").at("arms").get<int>());
		EXPECT_EQ(w.GetDamage(HitTypeLegs), val.at("damage").at("legs").get<int>());
		EXPECT_EQ(w.GetDamage(HitTypeBlock), val.at("damage").at("block").get<int>());

		EXPECT_EQ(w.GetPelletSize(), val.at("pellets").get<int>());
		EXPECT_EQ(w.GetClipSize(), val.at("clip").get<int>());
		EXPECT_EQ(w.GetMaxStock(), val.at("stock").get<int>());
		EXPECT_EQ(w.IsReloadSlow(), val.at("reload_slow").get<bool>());

		EXPECT_NEAR(static_cast<double>(w.GetSpread()), val.at("spread").get<double>(), POSITION_TOL);
		EXPECT_NEAR(static_cast<double>(w.GetDelay()), val.at("delay_s").get<double>(), POSITION_TOL);
		EXPECT_NEAR(static_cast<double>(w.GetReloadTime()), val.at("reload_s").get<double>(),
		            POSITION_TOL);

		// The weapon-name / protocol strings tie the fixture to its variant.
		EXPECT_EQ(std::string(WeaponName(type)), val.at("weapon").get<std::string>());
	}

	// Recoil multiplier rules are deterministic constants (RESEARCH Pitfall 3/4):
	// spread modifiers are aim ÷2 and crouch ÷2 (NOT for shotgun); recoil
	// modifiers are walking-not-aiming ×2, airborne ×2, crouch ÷2. These are
	// characterized as named scalar RULES + the base GetRecoil()/GetSpread()
	// vectors — NOT a fired-shot trace through Player::FireWeapon (which reads
	// cg_classicWeaponRecoil + world.GetTimeMS()).
	nlohmann::json BuildBaseVectors(ProtocolVersion version) {
		nlohmann::json out = nlohmann::json::object();
		const WeaponType types[3] = {RIFLE_WEAPON, SMG_WEAPON, SHOTGUN_WEAPON};
		for (WeaponType t : types) {
			WeaponHarness h(t, version);
			client::Weapon& w = h.W();
			Vector2 recoil = w.GetRecoil();
			out[WeaponName(t)] = {
			    {"spread", static_cast<double>(w.GetSpread())},
			    {"recoil_x", static_cast<double>(recoil.x)},
			    {"recoil_y", static_cast<double>(recoil.y)},
			};
		}
		return out;
	}

} // namespace

// ===========================================================================
// DISABLED_ generators — run once manually, commit frozen fixtures.
// ===========================================================================

TEST(DISABLED_WeaponDamageGenerate, Rifle075) {
	SettingsGuard guard;
	WeaponHarness h(RIFLE_WEAPON, ProtocolVersion::v075);
	auto j = BuildWeaponFixtureEnvelope("weap_value_lookup_rifle_075", "0.75");
	j["expected"]["value"] = BuildWeaponValue(h.W(), RIFLE_WEAPON, "0.75");
	WriteWeaponFixture("weap_value_lookup_rifle_075.json", j);
}

TEST(DISABLED_WeaponDamageGenerate, Smg075) {
	SettingsGuard guard;
	WeaponHarness h(SMG_WEAPON, ProtocolVersion::v075);
	auto j = BuildWeaponFixtureEnvelope("weap_value_lookup_smg_075", "0.75");
	j["expected"]["value"] = BuildWeaponValue(h.W(), SMG_WEAPON, "0.75");
	WriteWeaponFixture("weap_value_lookup_smg_075.json", j);
}

TEST(DISABLED_WeaponDamageGenerate, Shotgun075) {
	SettingsGuard guard;
	WeaponHarness h(SHOTGUN_WEAPON, ProtocolVersion::v075);
	auto j = BuildWeaponFixtureEnvelope("weap_value_lookup_shotgun_075", "0.75");
	j["expected"]["value"] = BuildWeaponValue(h.W(), SHOTGUN_WEAPON, "0.75");
	WriteWeaponFixture("weap_value_lookup_shotgun_075.json", j);
}

TEST(DISABLED_WeaponDamageGenerate, Rifle076) {
	SettingsGuard guard;
	WeaponHarness h(RIFLE_WEAPON, ProtocolVersion::v076);
	auto j = BuildWeaponFixtureEnvelope("weap_value_lookup_rifle_076", "0.76");
	j["expected"]["value"] = BuildWeaponValue(h.W(), RIFLE_WEAPON, "0.76");
	WriteWeaponFixture("weap_value_lookup_rifle_076.json", j);
}

TEST(DISABLED_WeaponDamageGenerate, Smg076) {
	SettingsGuard guard;
	WeaponHarness h(SMG_WEAPON, ProtocolVersion::v076);
	auto j = BuildWeaponFixtureEnvelope("weap_value_lookup_smg_076", "0.76");
	j["expected"]["value"] = BuildWeaponValue(h.W(), SMG_WEAPON, "0.76");
	WriteWeaponFixture("weap_value_lookup_smg_076.json", j);
}

TEST(DISABLED_WeaponDamageGenerate, Shotgun076) {
	SettingsGuard guard;
	WeaponHarness h(SHOTGUN_WEAPON, ProtocolVersion::v076);
	auto j = BuildWeaponFixtureEnvelope("weap_value_lookup_shotgun_076", "0.76");
	j["expected"]["value"] = BuildWeaponValue(h.W(), SHOTGUN_WEAPON, "0.76");
	WriteWeaponFixture("weap_value_lookup_shotgun_076.json", j);
}

// Modifier rules: base spread/recoil vectors per variant (both versions) plus
// the multiplier RULE constants. Characterized as data, NOT a fired trace.
TEST(DISABLED_WeaponModifierGenerate, Rules) {
	SettingsGuard guard;
	auto j = BuildWeaponFixtureEnvelope("weap_value_lookup_modifiers", "0.75");
	j["expected"]["value"] = {
	    {"base_075", BuildBaseVectors(ProtocolVersion::v075)},
	    {"base_076", BuildBaseVectors(ProtocolVersion::v076)},
	    // Spread modifiers (Player.cpp:554-563): aim (secondary) halves spread;
	    // crouch halves spread EXCEPT for the shotgun.
	    {"spread_rules",
	     {{"aim_multiplier", 0.5},
	      {"crouch_multiplier", 0.5},
	      {"crouch_applies_to_shotgun", false}}},
	    // Recoil modifiers (Player.cpp:786-793, cg_classicWeaponRecoil branch):
	    // walking-and-not-aiming doubles recoil; airborne doubles recoil; crouch
	    // halves recoil. These are the multiplier RULES, not a fired-shot trace
	    // (the live path also reads world.GetTimeMS() — Pitfall 4).
	    {"recoil_rules",
	     {{"walking_not_aiming_multiplier", 2.0},
	      {"airborne_multiplier", 2.0},
	      {"crouch_multiplier", 0.5}}},
	    {"note",
	     "Spread/recoil modifiers are characterized as pure constant RULES + base "
	     "vectors (value_lookup). Recoil is NOT a fired-shot trace: Player::FireWeapon "
	     "reads cg_classicWeaponRecoil and world.GetTimeMS(), which are config- and "
	     "tick-dependent (RESEARCH Pitfalls 3/4)."},
	};
	WriteWeaponFixture("weap_value_lookup_modifiers.json", j);
}

// ===========================================================================
// Enabled replay tests — run on every build, load frozen fixtures, assert.
// ===========================================================================

TEST_F(WeaponTestBase, Rifle075) {
	ReplayWeaponFixture("weap_value_lookup_rifle_075.json", RIFLE_WEAPON,
	                    ProtocolVersion::v075);
}
TEST_F(WeaponTestBase, Smg075) {
	ReplayWeaponFixture("weap_value_lookup_smg_075.json", SMG_WEAPON, ProtocolVersion::v075);
}
TEST_F(WeaponTestBase, Shotgun075) {
	ReplayWeaponFixture("weap_value_lookup_shotgun_075.json", SHOTGUN_WEAPON,
	                    ProtocolVersion::v075);
}
TEST_F(WeaponTestBase, Rifle076) {
	ReplayWeaponFixture("weap_value_lookup_rifle_076.json", RIFLE_WEAPON,
	                    ProtocolVersion::v076);
}
TEST_F(WeaponTestBase, Smg076) {
	ReplayWeaponFixture("weap_value_lookup_smg_076.json", SMG_WEAPON, ProtocolVersion::v076);
}
TEST_F(WeaponTestBase, Shotgun076) {
	ReplayWeaponFixture("weap_value_lookup_shotgun_076.json", SHOTGUN_WEAPON,
	                    ProtocolVersion::v076);
}

// ModifierRules: reconstruct the base vectors from the oracle and assert they
// match the frozen fixture; assert the multiplier-rule scalars exactly. All
// expectations read from JSON (the fixture is the portable contract).
TEST_F(WeaponTestBase, ModifierRules) {
	nlohmann::json j = LoadFixtureJson("weap_value_lookup_modifiers.json");
	const auto& val = j.at("expected").at("value");

	nlohmann::json base075 = BuildBaseVectors(ProtocolVersion::v075);
	nlohmann::json base076 = BuildBaseVectors(ProtocolVersion::v076);
	ExpectSnapshotMatches(val.at("base_075"), base075, "expected.value.base_075");
	ExpectSnapshotMatches(val.at("base_076"), base076, "expected.value.base_076");

	const auto& sr = val.at("spread_rules");
	EXPECT_NEAR(sr.at("aim_multiplier").get<double>(), 0.5, POSITION_TOL);
	EXPECT_NEAR(sr.at("crouch_multiplier").get<double>(), 0.5, POSITION_TOL);
	EXPECT_EQ(sr.at("crouch_applies_to_shotgun").get<bool>(), false);

	const auto& rr = val.at("recoil_rules");
	EXPECT_NEAR(rr.at("walking_not_aiming_multiplier").get<double>(), 2.0, POSITION_TOL);
	EXPECT_NEAR(rr.at("airborne_multiplier").get<double>(), 2.0, POSITION_TOL);
	EXPECT_NEAR(rr.at("crouch_multiplier").get<double>(), 0.5, POSITION_TOL);
}
