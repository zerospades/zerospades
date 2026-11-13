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
		// Dense light-gray smoke screen left where a grenade detonates
		// while cg_smokeGrenade is on (smoke-grenade re-skin of the
		// Grenade slot). Purely cosmetic: server damage/hit is still
		// computed for a classic grenade.
		//   - dense light-gray puffs emitted for EmitTime (10 s)
		//   - every puff fades over DissipateTime (5 s) after its spawn
		//     so the whole cloud is gone at TotalTime (15 s)
		//   - plays ONE positional 10 s "pssshhh" sample (no loop:
		//     IAudioDevice::Play returns no handle, so the sample
		//     itself must last EmitTime)
		class SmokeGrenadeEntity : public ILocalEntity {
			Client &client;
			IAudioDevice *audioDevice;
			Handle<IAudioChunk> soundChunk; // may be null when muted
			Vector3 origin;
			float age = 0.f;
			float puffTimer = 0.f;

		public:
			// ---- Tunables ------------------------------------------
			static constexpr float EmitTime = 10.f;       // s, diffusion
			static constexpr float DissipateTime = 5.f;   // s, dissipation
			static constexpr float TotalTime = EmitTime + DissipateTime;
			static constexpr float PuffInterval = 0.025f;  // s between puffs

			SmokeGrenadeEntity(Client &client, IAudioDevice *audioDevice,
			                   Handle<IAudioChunk> soundChunk,
			                   Vector3 origin);

			bool Update(float dt) override;
			void Render3D() override {} // puffs are their own entities
		};
	} // namespace client
} // namespace spades
