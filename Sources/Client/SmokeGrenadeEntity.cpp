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

#include "SmokeGrenadeEntity.h"
#include "Client.h"
#include "IAudioDevice.h"
#include "SmokeSpriteEntity.h"
#include <Core/Settings.h>

// Live-tunable volume of the smoke "pssshhh" (console, no rebuild):
//   cg_smokeGrenadeVolume 0.5   = quieter
//   cg_smokeGrenadeVolume 2     = louder
DEFINE_SPADES_SETTING(cg_smokeGrenadeVolume, "1");

namespace spades {
	namespace client {

		SmokeGrenadeEntity::SmokeGrenadeEntity(
		  Client &cli, IAudioDevice *audio, Handle<IAudioChunk> chunk,
		  Vector3 pos)
		    : client(cli), audioDevice(audio), soundChunk(std::move(chunk)),
		      origin(pos) {
			SPADES_MARK_FUNCTION();

			// single positional "pssshhh", lasting the whole emission
			// phase (the sample itself is ~EmitTime long)
			if (audioDevice && soundChunk) {
				AudioParam param;
				param.volume = (float)cg_smokeGrenadeVolume;
				audioDevice->Play(soundChunk.GetPointerOrNull(), origin,
				                  param);
			}
		}

		bool SmokeGrenadeEntity::Update(float dt) {
			SPADES_MARK_FUNCTION();
			age += dt;

			// --- dense light-gray puffs ---
			puffTimer -= dt;
			while (puffTimer <= 0.f) {
				puffTimer += PuffInterval;

				// spawn mostly around the origin, slight upward bias
				// (z axis is inverted in this engine: negative z = up)
				// cloud-widening patch: horizontal spawn offset *5
				Vector3 off = RandomUnitVector();
				off.z = fabsf(off.z) * 0.4f;
				off.x *= 5.f;   // widen: horizontal dispersion x5
				off.y *= 5.f;   // widen: horizontal dispersion x5

				auto ent = stmp::make_unique<SmokeSpriteEntity>(
				  client, MakeVector4(0.82f, 0.82f, 0.82f, 0.98f), 60.f,
				  SmokeSpriteEntity::Type::Steady);
				// cloud-widening patch: horizontal velocity x5
				// vertical component unchanged (height already tuned;
				// z inverted: negative = upward)
				Vector3 vel = MakeVector3(
				  (SampleRandomFloat() - 0.5f) * 8.f,
				  (SampleRandomFloat() - 0.5f) * 8.f,
				  -4.35f + SampleRandomFloat() * -4.3f);
				ent->SetTrajectory(origin + off, vel, 0.94f, 0.f);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				// thick: starts around 3.5 units, keeps growing
				// (sprite-size patch: x3.5 to compensate the x5 cloud width)
				ent->SetRadius(3.2f + SampleRandomFloat() * 1.5f, 0.8f, 0.3f);
				// every puff born at time t lives until TotalTime, with a
				// slow DissipateTime fade-out at the end: the cloud stops
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
