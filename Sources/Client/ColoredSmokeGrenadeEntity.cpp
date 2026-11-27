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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "ColoredSmokeGrenadeEntity.h"
#include "Client.h"
#include "IAudioDevice.h"
#include "SmokeSpriteEntity.h"
#include <Core/Settings.h>

// The volume CVar is DEFINED once in SmokeGrenadeEntity.cpp; here it is
// only declared extern. A second DEFINE_SPADES_SETTING of the same name
// would be a linker error.
//   cg_smokeGrenadeVolume 0.5 = quieter, 2 = louder (console, no rebuild)
SPADES_SETTING(cg_smokeGrenadeVolume);

namespace spades {
	namespace client {

		ColoredSmokeGrenadeEntity::ColoredSmokeGrenadeEntity(
		  Client &cli, IAudioDevice *audio, Handle<IAudioChunk> chunk,
		  Vector3 pos, Vector4 col)
		    : client(cli), audioDevice(audio), soundChunk(std::move(chunk)),
		      origin(pos), smokeColor(col) {
			SPADES_MARK_FUNCTION();

			// first positional "pssshhh" at spawn; Update re-triggers
			// it ONCE more at SecondSoundDelay (Play returns no
			// handle, hence the manual re-trigger)
			if (audioDevice && soundChunk) {
				AudioParam param;
				param.volume = (float)cg_smokeGrenadeVolume;
				audioDevice->Play(soundChunk.GetPointerOrNull(), origin,
				                  param);
			}
		}

		bool ColoredSmokeGrenadeEntity::Update(float dt) {
			SPADES_MARK_FUNCTION();
			age += dt;

			// --- second (and last) "pssshhh" at 8.5 s: the sample is
			// ~9 s, so it plays 0-9 s and 8.5-17.5 s with a small
			// overlap instead of a hard cut. After that the emission
			// continues silently until EmitTime (20 s).
			if (!secondSoundPlayed && age >= SecondSoundDelay) {
				secondSoundPlayed = true;
				if (audioDevice && soundChunk) {
					AudioParam param;
					param.volume = (float)cg_smokeGrenadeVolume;
					audioDevice->Play(soundChunk.GetPointerOrNull(),
					                  origin, param);
				}
			}

			// --- thin colored puffs ---
			puffTimer -= dt;
			while (puffTimer <= 0.f) {
				puffTimer += PuffInterval;

				// spawn mostly around the origin, slight upward bias
				// (z axis is inverted in this engine: negative z = up)
				Vector3 off = RandomUnitVector();
				off.z = fabsf(off.z) * 0.4f;

				auto ent = stmp::make_unique<SmokeSpriteEntity>(
				  client, smokeColor, 60.f,
				  SmokeSpriteEntity::Type::Steady);
				Vector3 vel = MakeVector3(
				  (SampleRandomFloat() - 0.5f) * 0.6f,
				  (SampleRandomFloat() - 0.5f) * 0.6f,
				  // z axis is inverted in this engine: NEGATIVE = upward.
				  // Same rise speed as the gray variant so the column
				  // reaches the same height (tuned value, do not revert
				  // to the old positive value: it stays at ground level)
				  -7.0f + SampleRandomFloat() * -5.0f);
				ent->SetTrajectory(origin + off * 0.6f, vel, 0.9f, 0.f);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				// thin column: small puffs (~1 unit), slow growth
				ent->SetRadius(0.9f + SampleRandomFloat() * 0.5f, 0.22f, 0.3f);
				// every puff born at time t lives until TotalTime, with a
				// slow DissipateTime fade-out at the end: the column stops
				// growing at 10 s and is fully gone at 15 s
				float life = TotalTime - age + SampleRandomFloat() * 0.5f;
				ent->SetLifeTime(life, 0.25f, DissipateTime);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				client.AddLocalEntity(std::move(ent));
			}

			// emission done at EmitTime; the remaining fade-out belongs
			// to the puffs themselves
			return age < EmitTime;
		}
	} // namespace client
} // namespace spades