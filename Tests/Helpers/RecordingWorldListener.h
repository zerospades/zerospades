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

#include <memory>
#include <vector>

#include <Client/IWorldListener.h>
#include <Core/Math.h>

namespace spades {
	namespace tests {

		/**
		 * IWorldListener implementation that records notable events for test assertions.
		 * All other callbacks are no-ops.
		 */
		struct RecordingWorldListener : public client::IWorldListener {
			// Recorded event counters / data
			std::vector<IntVector3> lastBlocksFell;

			// All BlocksFell invocations in order (one entry per cluster callback).
			// Use this for multi-cluster fixture assertions that need to verify
			// callback count AND per-cluster cell sets independently.
			// lastBlocksFell is kept for backwards-compat (single-cluster tests).
			std::vector<std::vector<IntVector3>> allBlocksFell;
			int blocksFellCallCount = 0;

			int playerFiredCount = 0;
			int grenadeExplodedCount = 0;
			int grenadeWaterCount = 0;   // GrenadeDroppedIntoWater fired (WEAP-03b water landing)
			int grenadeBouncedCount = 0; // GrenadeBounced fired (WEAP-03a explicit bounce assertion)
			int killCount = 0;

			// --- Player movement / state (no-ops) ---
			void PlayerObjectSet(int /*playerId*/) override {}
			void PlayerMadeFootstep(client::Player&) override {}
			void PlayerJumped(client::Player&) override {}
			void PlayerLanded(client::Player&, bool /*hurt*/) override {}
			void PlayerEjectedBrass(client::Player&) override {}
			void PlayerDryFiredWeapon(client::Player&) override {}
			void PlayerReloadingWeapon(client::Player&) override {}
			void PlayerReloadedWeapon(client::Player&) override {}
			void PlayerChangedTool(client::Player&) override {}
			void PlayerPulledGrenadePin(client::Player&) override {}
			void PlayerThrewGrenade(client::Player&,
			                        stmp::optional<const client::Grenade&>) override {}
			void PlayerMissedSpade(client::Player&) override {}
			void PlayerHitBlockWithSpade(client::Player&, Vector3 /*hitPos*/,
			                             IntVector3 /*blockPos*/,
			                             IntVector3 /*normal*/) override {}
			void PlayerRestocked(client::Player&) override {}

			// --- Bullet / grenade events (no-ops, except recorded ones) ---
			void BulletHitPlayer(client::Player& /*hurtPlayer*/, HitType /*hitType*/,
			                     Vector3 /*hitPos*/, client::Player& /*by*/,
			                     std::unique_ptr<client::IBulletHitScanState>& /*stateCell*/)
			    override {}
			void BulletNearPlayer(client::Player&) override {}
			void BulletHitBlock(Vector3 /*hitPos*/, IntVector3 /*blockPos*/,
			                    IntVector3 /*normal*/) override {}
			void AddBulletTracer(client::Player& /*player*/, Vector3 /*muzzlePos*/,
			                     Vector3 /*hitPos*/) override {}
			void GrenadeBounced(const client::Grenade&) override { ++grenadeBouncedCount; }
			void GrenadeDroppedIntoWater(const client::Grenade&) override { ++grenadeWaterCount; }

			// --- Local player events (no-ops) ---
			void LocalPlayerBlockAction(IntVector3,
			                            BlockActionType /*type*/) override {}
			void LocalPlayerCreatedLineBlock(IntVector3, IntVector3) override {}
			void LocalPlayerHurt(HurtType /*type*/, Vector3 /*source*/) override {}
			void LocalPlayerBuildError(client::BuildFailureReason /*reason*/) override {}

			// --- Recorded callbacks ---
			void PlayerFiredWeapon(client::Player&) override { ++playerFiredCount; }
			void PlayerKilledPlayer(client::Player& /*killer*/, client::Player& /*victim*/,
			                        KillType) override {
				++killCount;
			}
			void GrenadeExploded(const client::Grenade&) override { ++grenadeExplodedCount; }
			void BlocksFell(std::vector<IntVector3> blocks) override;
		};

	} // namespace tests
} // namespace spades
