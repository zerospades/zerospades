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

#pragma once

#include "Client.h"
#include "ILocalEntity.h"
#include "IAudioChunk.h"
#include <Core/Math.h>

namespace spades {
	class IAudioDevice;

	namespace client {
		// Thin colored smoke column left where a grenade detonates while
		// cg_smokeGrenadeStyle is 1 (second smoke-grenade mode). The smoke
		// color is chosen at construction time by the caller (usually the
		// thrower's team color); this class does not know anything about
		// teams. Purely cosmetic: server damage/hit is still computed for
		// a classic grenade.
		//   - thin column of small puffs emitted for EmitTime (20 s)
		//   - every puff fades over DissipateTime (10 s) after its spawn
		//     so the whole column is gone at TotalTime (30 s)
		//   - plays the ~9 s "pssshhh" sample TWICE: once at spawn
		//     (constructor) and once at SecondSoundDelay (8.5 s) so
		//     the sound covers ~17.5 s of the 20 s emission
		//     (IAudioDevice::Play returns no handle, so the entity
		//     re-triggers the sample itself; shared volume CVar
		//     cg_smokeGrenadeVolume, defined in SmokeGrenadeEntity.cpp;
		//     declared extern here via SPADES_SETTING, so NO DEFINE in
		//     this file: a duplicate DEFINE of the same CVar name is a
		//     linker error)
		class ColoredSmokeGrenadeEntity : public ILocalEntity {
			Client &client;
			IAudioDevice *audioDevice;
			Handle<IAudioChunk> soundChunk; // may be null when muted
			Vector3 origin;
			Vector4 smokeColor; // passed by the caller (e.g. team color)
			float age = 0.f;
			float puffTimer = 0.f;
			bool secondSoundPlayed = false; // guards the single re-trigger

		public:
			// ---- Tunables ------------------------------------------
			// duration x2 compared to the gray variant (default was
			// 10 s emission + 5 s dissipation)
			static constexpr float EmitTime = 20.f;       // s, diffusion
			static constexpr float DissipateTime = 10.f;   // s, dissipation
			static constexpr float TotalTime = EmitTime + DissipateTime;
			static constexpr float PuffInterval = 0.07f;  // s between puffs
			// delay of the SECOND (and last) "pssshhh" re-trigger.
			// The sample is ~9 s: it plays from 0-9 s (constructor)
			// and 8.5-17.5 s (small 0.5 s crossfade overlap, no cut)
			static constexpr float SecondSoundDelay = 8.5f; // s

			ColoredSmokeGrenadeEntity(Client &client, IAudioDevice *audioDevice,
			                          Handle<IAudioChunk> soundChunk,
			                          Vector3 origin, Vector4 smokeColor);

			bool Update(float dt) override;
			void Render3D() override {} // puffs are their own entities
		};
	} // namespace client
} // namespace spades