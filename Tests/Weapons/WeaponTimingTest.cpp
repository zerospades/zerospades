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

// WEAP-02: fire-rate gate, ammo decrement, empty-clip dry-fire, reload timing.
//
// Pattern: generate-then-freeze (step_trace). One scripted SMG (v0.75) sequence
// drives Weapon::FrameNext / Reload / ReloadDone at FIXED_DT, capturing
// per-tick {tick, ammo, stock, reloading, fired, next_shot_time}. The frozen
// expected.ticks[] are replayed by re-running the IDENTICAL scripted sequence
// and comparing tick-by-tick (integers exact, next_shot_time via tolerance).
//
// Drive seam (Weapon.cpp:88-151): the weapon owns its own `time` field; no
// World tick is needed. Reset() sets ammo=GetClipSize, stock=GetMaxStock,
// time=0, nextShotTime=0. With SetShooting(true), FrameNext fires when
// time>=nextShotTime && ammo>0 (for a local player), consuming one round and
// advancing nextShotTime by GetDelay(); otherwise the rate gate suppresses it.
//
// Local-player gate (RESEARCH Pattern 3): FrameNext only blocks empty-clip fire
// when owner.IsLocalPlayer(). The owning Player is registered as the local
// player via World::SetLocalPlayerIndex(0) so the empty-clip tick dry-fires
// instead of firing forever.
//
// Reload-done timing: for a LOCAL player, FrameNext does NOT auto-complete a
// reload (that branch is !ownerIsLocalPlayer only, Weapon.cpp:125). Local
// reload completion is server-driven via ReloadDone(ammo, stock). The trace
// therefore calls Reload(), advances a couple ticks to observe reloading==true,
// then calls ReloadDone(clip, stock) and observes reloading==false with ammo
// restored — proving the reload start/done transitions without ticking through
// the full 2.5s reload window.
//
// Nyquist sampling (RESEARCH Validation §): the SMG's 6-tick cadence (delay
// 0.1s at FIXED_DT) is captured EVERY tick across the full empty cycle, so the
// fixture contains the fire-allowed ticks (ammo--), the intervening suppressed
// ticks (ammo stable — proves the rate gate), the empty-clip dry-fire tick
// (fired==false, ammo==0), the reload-start tick, and the reload-done tick.

#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <Client/GameConstants.h>
#include <Client/GameProperties.h>
#include <Client/Player.h>
#include <Client/Weapon.h>
#include <Client/World.h>

#include "HeadlessWorld.h"
#include "MakeFlatMap.h"
#include "SettingsGuard.h"
#include "ToleranceMatchers.h"
#include "WeaponTestBase.h"

using namespace spades;
using namespace spades::tests;
using namespace spades::client;

namespace {

	// nextShotTime is a private field with no getter; GetTimeToNextFire() returns
	// (nextShotTime - time). Capturing time-to-next-fire is the public-API proxy
	// for the rate-gate boundary and is itself a meaningful characterized scalar.
	nlohmann::json SnapshotWeaponTick(client::Weapon& w, int tick, bool fired) {
		return nlohmann::json{
		    {"tick", tick},
		    {"ammo", w.GetAmmo()},
		    {"stock", w.GetStock()},
		    {"reloading", w.IsReloading()},
		    {"fired", fired},
		    {"time_to_next_fire", static_cast<double>(w.GetTimeToNextFire())},
		};
	}

	// Run the scripted SMG (v075) fire/empty/dry-fire/reload/done sequence,
	// capturing one snapshot per FrameNext call. Identical in the generator and
	// the replay so the comparison is non-tautological (the oracle is re-run, the
	// frozen JSON is only the expected output). Returns the ticks[] array.
	//
	// The clip is emptied at the natural 6-tick cadence; once empty we capture a
	// few dry-fire ticks, then Reload(), a couple reloading ticks, ReloadDone(),
	// and a final post-reload tick.
	nlohmann::json RunFireReloadSequence() {
		HeadlessWorld hw(42, MakeFlatMapBytes());
		auto p = std::make_unique<client::Player>(hw.GetWorld(), 0, SMG_WEAPON, 0);
		hw.GetWorld().SetPlayer(0, std::move(p));
		hw.GetWorld().SetLocalPlayerIndex(0); // local-player gate (empty-clip dry-fire)
		auto opt = hw.GetWorld().GetPlayer(0);
		client::Player& player = *opt;
		client::Weapon& w = player.GetWeapon();

		w.Reset();
		w.SetShooting(true);

		const int clip = w.GetClipSize(); // 30 for SMG
		nlohmann::json ticks = nlohmann::json::array();
		int tick = 0;

		// Phase 1: fire until the clip is empty, then a handful of dry-fire ticks.
		// At the 6-tick cadence emptying 30 rounds takes ~180 ticks; the +12 tail
		// captures the empty-clip dry-fire ticks (fired==false, ammo==0).
		const int fireTicks = clip * 6 + 12;
		for (int i = 0; i < fireTicks; i++, tick++) {
			bool fired = w.FrameNext(FIXED_DT);
			ticks.push_back(SnapshotWeaponTick(w, tick, fired));
		}

		// Phase 2: reload. SetShooting(false) so the local-player Reload() gate
		// (IsReloadSlow && shooting && ammo>0) is irrelevant; the SMG is not slow
		// anyway. Reload() sets reloading=true.
		w.SetShooting(false);
		w.Reload();
		// Capture two ticks with reloading==true (reload in progress).
		for (int i = 0; i < 2; i++, tick++) {
			bool fired = w.FrameNext(FIXED_DT);
			ticks.push_back(SnapshotWeaponTick(w, tick, fired));
		}

		// Phase 3: server-driven completion for the local player. ReloadDone
		// restocks the clip and clears reloading. Capture the done state, then one
		// more FrameNext tick to confirm the cleared reloading flag persists.
		w.ReloadDone(clip, w.GetStock() - clip);
		ticks.push_back(SnapshotWeaponTick(w, tick++, false));
		{
			bool fired = w.FrameNext(FIXED_DT);
			ticks.push_back(SnapshotWeaponTick(w, tick++, fired));
		}

		return ticks;
	}

} // namespace

// ===========================================================================
// DISABLED_ generator — run once manually, commit the frozen fixture.
// ===========================================================================

TEST(DISABLED_WeaponTimingGenerate, FireReload) {
	SettingsGuard guard;
	nlohmann::json ticks = RunFireReloadSequence();
	auto j = BuildWeaponFixtureEnvelope("weap_step_trace_fire_reload", "0.75", "step_trace");
	j["expected"]["ticks"] = ticks;
	WriteWeaponFixture("weap_step_trace_fire_reload.json", j);
}

// ===========================================================================
// Enabled replay test — load the frozen fixture, re-run, assert tick-by-tick.
// ===========================================================================

TEST_F(WeaponTestBase, FireReload) {
	nlohmann::json j = LoadFixtureJson("weap_step_trace_fire_reload.json");
	const auto& want = j.at("expected").at("ticks");

	nlohmann::json got = RunFireReloadSequence();

	ASSERT_EQ(want.size(), got.size()) << "tick count mismatch";
	for (size_t i = 0; i < want.size(); i++)
		ExpectSnapshotMatches(want[i], got[i],
		                      "expected.ticks[" + std::to_string(i) + "]");

	// Anti-tautology cross-checks read from the frozen JSON: prove the trace
	// actually contains a fire-allowed tick (ammo decremented), a rate-gate
	// suppressed tick (fired==false while ammo>0), an empty-clip dry-fire tick
	// (fired==false, ammo==0), and a reload-done tick (reloading false, ammo
	// restored to a full clip).
	bool sawFire = false, sawRateGate = false, sawDryFire = false, sawReloadDone = false;
	int clip = 0;
	for (const auto& t : want) {
		int ammo = t.at("ammo").get<int>();
		bool fired = t.at("fired").get<bool>();
		bool reloading = t.at("reloading").get<bool>();
		if (ammo > clip)
			clip = ammo; // the full clip size is the max ammo observed
		if (fired)
			sawFire = true;
		if (!fired && ammo > 0 && !reloading)
			sawRateGate = true;
		if (!fired && ammo == 0)
			sawDryFire = true;
	}
	for (const auto& t : want) {
		if (!t.at("reloading").get<bool>() && t.at("ammo").get<int>() == clip &&
		    t.at("tick").get<int>() > 0)
			sawReloadDone = true;
	}
	EXPECT_TRUE(sawFire) << "no fire-allowed tick in trace";
	EXPECT_TRUE(sawRateGate) << "no rate-gate-suppressed tick in trace";
	EXPECT_TRUE(sawDryFire) << "no empty-clip dry-fire tick in trace";
	EXPECT_TRUE(sawReloadDone) << "no reload-done (clip-restored) tick in trace";
}
