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

#include "ILocalEntity.h"
#include "IModel.h"
#include <Core/Math.h>

namespace spades {
	namespace client {
		class IRenderer;
		class Client;
		class IAudioChunk;
		class GunCasing : public ILocalEntity {
			Client* client;
			IRenderer& renderer;
			IModel* model;
			float scale;
			AABB3 box; // bounding box of the model, cached
			Matrix4 matrix;
			Vector3 velocity;
			Vector2 rot; // angular speed around local X / Y, in rad/s
			float time;
			bool resting;
			float groundZ; // z of the surface the item landed on

			IAudioChunk* dropSound;
			IAudioChunk* waterSound;

		public:
			// TODO: Replace pointers with references
			GunCasing(Client* client, IModel* model, IAudioChunk* dropSound,
			          IAudioChunk* waterSound, Vector3 pos, Vector3 dir, Vector3 flyDir, float lifeTime);
			~GunCasing();
			bool Update(float dt) override;
			void Render3D() override;
		};
	} // namespace client
} // namespace spades