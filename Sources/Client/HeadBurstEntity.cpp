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

#include "HeadBurstEntity.h"
#include "Client.h"
#include "GameConstants.h"
#include "GameMap.h"
#include "IAudioChunk.h"
#include "IAudioDevice.h"
#include "IRenderer.h"
#include "ParticleSpriteEntity.h"
#include "World.h"
#include <Core/Settings.h>

// Comic gore: burst the skull into voxel chunks on a headshot kill.
// Enabled by default; missing models/sound degrade gracefully.
DEFINE_SPADES_SETTING(cg_headshotGore, "1");
DEFINE_SPADES_SETTING(cg_headshotGoreChunks, "8");
DEFINE_SPADES_SETTING(cg_headshotGoreScale, "1.0");
DEFINE_SPADES_SETTING(cg_headshotGoreVolume, "3");
SPADES_SETTING(cg_particles);
SPADES_SETTING(cg_blood);

namespace spades {
	namespace client {
		namespace {
			// total chunk lifetime, in seconds
			constexpr float CHUNK_LIFETIME = 8.0F;
			// gravity (the z axis points down), same as GunCasing
			constexpr float CHUNK_GRAVITY = 32.0F;
			// velocity kept after a bounce
			constexpr float CHUNK_RESTITUTION = 0.46F;
			// burst sound / blood splat range (squared)
			constexpr float BURST_DIST_SQ = 40.0F * 40.0F;

			// chunk model files, installed in Resources/Models/Effects/HeadBurst/
			const char* const kChunkModels[] = {
				"Models/Effects/HeadBurst/Chunk1.kv6",
				"Models/Effects/HeadBurst/Chunk2.kv6",
				"Models/Effects/HeadBurst/Chunk3.kv6",
				"Models/Effects/HeadBurst/Chunk4.kv6",
				"Models/Effects/HeadBurst/Chunk5.kv6",
				"Models/Effects/HeadBurst/Chunk6.kv6",
				"Models/Effects/HeadBurst/Chunk7.kv6",
			};

			// burst sound files, installed in Resources/Sounds/Effects/HeadBurst/
			const char* const kBurstSounds[] = {
				"Sounds/Effects/HeadBurst/HeadBurst1.opus",
				"Sounds/Effects/HeadBurst/HeadBurst2.opus",
				"Sounds/Effects/HeadBurst/HeadBurst3.opus",
				"Sounds/Effects/HeadBurst/HeadBurst4.opus",
			};
		}

		HeadBurstEntity::HeadBurstEntity(Client& client, Vector3 origin, Vector3 shotDir)
		    : client(&client), renderer(client.GetRenderer()), origin(origin) {

			// load whatever chunk models are installed; if none exist, the
			// entity removes itself on the first Update
			for (const char* name : kChunkModels) {
				try {
					models.push_back(renderer.RegisterModel(name));
				} catch (...) {
					// model file missing: skip it
				}
			}

			// optional burst sounds, picked randomly; stays silent when
			// none are installed
			for (const char* name : kBurstSounds) {
				try {
					IAudioChunk* chunk = client.GetAudioDevice().RegisterSound(name);
					chunk->AddRef();
					burstSounds.push_back(chunk);
				} catch (...) {
					// sound file missing: skip it
				}
			}

			if (!burstSounds.empty()) {
				float distSqr = (client.GetLastSceneDef().viewOrigin - origin).GetSquaredLength2D();
				if (!client.IsMuted() && distSqr < BURST_DIST_SQ) {
					AudioParam param;
					// volume is tunable so the burst can be heard over gunfire
					float burstVolume = cg_headshotGoreVolume; // explicit float read
					param.volume = burstVolume;
					// larger reference distance keeps the burst audible at range
					param.referenceDistance = 4.0F;
					param.pitch = 0.9F + SampleRandomFloat() * 0.2F;
					IAudioChunk* chunk =
						burstSounds[SampleRandomInt(0, (int)burstSounds.size() - 1)];
					client.GetAudioDevice().Play(chunk, origin, param);
				}
			}

			if (models.empty())
				return;

			// spawn the chunks in a fountain biased along the shot direction
			Vector3 dir = shotDir.Normalize();
			Vector3 up = MakeVector3(0, 0, 1);
			Vector3 right;
			if (dir.x * dir.x + dir.y * dir.y < 0.000001F) {
				// shot is vertical: pick any horizontal axis instead
				right = MakeVector3(1, 0, 0);
			} else {
				right = Vector3::Cross(dir, up).Normalize();
			}
			Vector3 upAdj = Vector3::Cross(right, dir).Normalize();

			int numChunks = cg_headshotGoreChunks;
			if (numChunks < 1)
				numChunks = 1;
			if (numChunks > 32)
				numChunks = 32;

			chunks.reserve(numChunks);
			float scaleBase = cg_headshotGoreScale; // explicit float read
			for (int i = 0; i < numChunks; i++) {
				Chunk c;
				c.matrix = Matrix4::Translate(origin);
				c.velocity = dir * (1.5F + SampleRandomFloat() * 3.5F)
				              + right * (SampleRandomFloat() - SampleRandomFloat()) * 3.0F
				              + upAdj * (SampleRandomFloat() - SampleRandomFloat()) * 3.0F
				              // upward geyser (the z axis points down)
				              + MakeVector3(0, 0, -1) * (3.0F + SampleRandomFloat() * 8.0F);
				c.angVel = MakeVector3(SampleRandomFloat() - SampleRandomFloat(),
				                       SampleRandomFloat() - SampleRandomFloat(),
				                       SampleRandomFloat() - SampleRandomFloat()) * 8.0F;
				c.scale = (0.025F + SampleRandomFloat() * 0.02F) * scaleBase;
				c.life = CHUNK_LIFETIME;
				c.modelIndex = SampleRandomInt(0, (int)models.size() - 1);
				c.splatDone = false;
				chunks.push_back(c);
			}
		}

		HeadBurstEntity::~HeadBurstEntity() {
			for (IAudioChunk* chunk : burstSounds)
				chunk->Release();
		}

		bool HeadBurstEntity::Update(float dt) {
			if (models.empty() || chunks.empty())
				return false;

			// fog cull on the burst origin
			float distSqr =
				(client->GetLastSceneDef().viewOrigin - origin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return false;

			const Handle<GameMap>& map = client->GetWorld()->GetMap();
			SPAssert(map);

			bool anyAlive = false;
			for (auto& c : chunks) {
				if (c.life <= 0.0F)
					continue;

				c.life -= dt;

				Matrix4 lastMat = c.matrix;

				// tumble
				c.matrix = c.matrix * Matrix4::Rotate(MakeVector3(1, 0, 0), c.angVel.x * dt);
				c.matrix = c.matrix * Matrix4::Rotate(MakeVector3(0, 1, 0), c.angVel.y * dt);
				// move in world space (left-multiply, same as GunCasing:
				// right-multiplying would bend the path through the tumble)
				c.matrix = Matrix4::Translate(c.velocity * dt) * c.matrix;
				c.velocity.z += CHUNK_GRAVITY * dt;

				IntVector3 lp = c.matrix.GetOrigin().Floor();

				// dropped into water: vanish with a blood splash
				if (lp.z >= 63) {
					c.life = 0.0F;
					if (!c.splatDone) {
						c.splatDone = true;
						if (distSqr < BURST_DIST_SQ)
							BloodSplat(*map, c.matrix.GetOrigin(), c.velocity);
					}
					continue;
				}

				if (map->ClipWorld(lp.x, lp.y, lp.z)) {
					// bounced off a surface (GunCasing collision logic)
					IntVector3 lp2 = lastMat.GetOrigin().Floor();
					if (lp.z != lp2.z &&
					    ((lp.x == lp2.x && lp.y == lp2.y) ||
					     !map->ClipWorld(lp.x, lp.y, lp2.z))) {
						c.velocity.z = -c.velocity.z;
					} else if (lp.x != lp2.x &&
					           ((lp.y == lp2.y && lp.z == lp2.z) ||
					            !map->ClipWorld(lp2.x, lp.y, lp.z))) {
						c.velocity.x = -c.velocity.x;
					} else if (lp.y != lp2.y &&
					           ((lp.x == lp2.x && lp.z == lp2.z) ||
					            !map->ClipWorld(lp.x, lp2.y, lp.z))) {
						c.velocity.y = -c.velocity.y;
					} else {
						// stuck in geometry: drop this chunk
						c.life = 0.0F;
						continue;
					}

					c.matrix = lastMat;
					c.velocity *= CHUNK_RESTITUTION;
					c.angVel *= CHUNK_RESTITUTION;

					// blood mark where the chunk lands (first impact only)
					if (!c.splatDone) {
						c.splatDone = true;
						if (distSqr < BURST_DIST_SQ)
							BloodSplat(*map, lastMat.GetOrigin(), c.velocity);
					}
				}

				if (c.life > 0.0F)
					anyAlive = true;
			}

			return anyAlive;
		}

		void HeadBurstEntity::Render3D() {
			if (models.empty() || chunks.empty())
				return;

			ModelRenderParam param;
			for (const auto& c : chunks) {
				if (c.life <= 0.0F)
					continue;
				param.matrix = c.matrix * Matrix4::Scale(c.scale);
				renderer.RenderModel(*models[c.modelIndex], param);
			}
		}
		void HeadBurstEntity::BloodSplat(GameMap& map, Vector3 pos, Vector3 vel) {
			// Client::Bleed is private; reproduce its particle look here
			if (!cg_particles)
				return;

			// persistent marks on the blocks (same gate as vanilla spatter)
			if ((int)cg_blood >= 2)
				client->SpatterBlood(pos, vel);

			Handle<IImage> img = renderer.RegisterWhiteImage();
			uint32_t col = IntVectorToColor(MakeIntVector3(127, 0, 0));
			col = map.GetColorJit(col); // jit the colour
			Vector4 color = ConvertColorRGBA(IntVectorFromColor(col));

			int splats = SampleRandomInt(2, 4);
			for (int i = 0; i < splats; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*client, img, color);
				Vector3 velBias = MakeVector3(0, 0, -0.5F);
				ent->SetTrajectory(pos, (RandomVector() + velBias * 0.5F) * 4.0F);
				ent->SetRadius(0.4F);
				ent->SetLifeTime(3.0F, 0.0F, 1.0F);
				ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				client->AddLocalEntity(std::move(ent));
			}
		}
	} // namespace client
} // namespace spades
