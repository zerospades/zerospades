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

#include "GunCasing.h"
#include "Client.h"
#include "GameMap.h"
#include "IAudioChunk.h"
#include "IAudioDevice.h"
#include "IRenderer.h"
#include "ParticleSpriteEntity.h"
#include "World.h"
#include <Core/Settings.h>

SPADES_SETTING(cg_particles);

namespace spades {
	namespace client {
		static const float kSpinSpeed = 5.0F; // angular speed around each axis, in rad/s
		static const float kBounceCorrection = 0.5F; // fraction of the side alignment applied per ground bounce
		static const float kShrinkSpeed = 3.0F;
		static const float kSettleRate = 8.0F;

		namespace {
			// Rotates the basis of `m` towards the "lying flat" pose.
			// In that pose the local X axis is vertical and the local Y axis is horizontal.
			// `amount` is 0 for no change and 1 for the full alignment.
			Matrix4 AlignToSide(const Matrix4& m, float amount) {
				const Vector3 ax = m.GetAxis(0);
				const Vector3 ay = m.GetAxis(1);
				const Vector3 az = m.GetAxis(2);

				// handedness of the current basis, so the result is never mirrored
				const float hand = (Vector3::Dot(Vector3::Cross(ay, az), ax) >= 0.0F) ? 1.0F : -1.0F;

				// target X: straight down or straight up (whichever is closer)
				const Vector3 tx = MakeVector3(0.0F, 0.0F, (ax.z >= 0.0F) ? 1.0F : -1.0F);

				// target Y: keep the heading, flattened onto the ground plane
				Vector3 ty = MakeVector3(ay.x, ay.y, 0.0F);
				if (ty.GetSquaredLength() < 0.01F) {
					ty = MakeVector3(az.x, az.y, 0.0F);
					if (ty.GetSquaredLength() < 0.0001F)
						ty = MakeVector3(0.0F, 1.0F, 0.0F);
				}
				ty = ty.Normalize();

				// blend the axes and re-orthonormalize them
				const Vector3 x = (ax + (tx - ax) * amount).Normalize();
				Vector3 y = ay + (ty - ay) * amount;
				y = (y - x * Vector3::Dot(y, x)).Normalize();
				const Vector3 z = Vector3::Cross(x, y) * hand;

				return Matrix4::FromAxis(x, y, z, m.GetOrigin());
			}

			// Offset from the origin to the lowest point of the model when it lies flat,
			// that is, when its local X axis is vertical (z points down).
			float LowestPointOffset(const Matrix4& m, const AABB3& box, float scale) {
				return ((m.GetAxis(0).z >= 0.0F) ? box.max.x : -box.min.x) * scale;
			}

			// Moves the origin vertically so the lowest point of the model sits on the ground.
			Matrix4 PlaceOnGround(const Matrix4& m, const AABB3& box, float scale, float groundZ) {
				Vector3 o = m.GetOrigin();
				o.z = groundZ - LowestPointOffset(m, box, scale);
				return Matrix4::FromAxis(m.GetAxis(0), m.GetAxis(1), m.GetAxis(2), o);
			}
		} // namespace

		GunCasing::GunCasing(Client* client, IModel* model, IAudioChunk* dropSound,
		                     IAudioChunk* waterSound, Vector3 pos, Vector3 dir, Vector3 flyDir, float lifeTime)
		    : client(client),
		      renderer(client->GetRenderer()),
		      model(model),
			  scale(0.0125F),
			  box(model->GetBoundingBox()),
			  time(lifeTime),
		      resting(false),
		      groundZ(0.0F),
		      dropSound(dropSound),
		      waterSound(waterSound) {
			if (dropSound)
				dropSound->AddRef();
			if (waterSound)
				waterSound->AddRef();

			Vector3 up = MakeVector3(0, 0, 1);
			Vector3 right = Vector3::Cross(dir, up).Normalize();
			up = Vector3::Cross(right, dir);
			matrix = Matrix4::FromAxis(right, dir, up, pos);
			velocity = flyDir * 10.0F;

			const int rotDir = SampleRandom() & 3;
			rot.x = ((rotDir & 1) ? 1.0F : -1.0F) * kSpinSpeed;
			rot.y = ((rotDir & 2) ? 1.0F : -1.0F) * kSpinSpeed;
		}
		GunCasing::~GunCasing() {
			if (dropSound)
				dropSound->Release();
			if (waterSound)
				waterSound->Release();
		}

		bool GunCasing::Update(float dt) {
			// the lifetime only runs while the object lies on the ground
			if (resting)
				time -= dt;
			// lifetime over
			if (time <= 0.0F)
				return false;

			const auto& viewOrigin = client->GetLastSceneDef().viewOrigin;
			float distSqr = (viewOrigin - matrix.GetOrigin()).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return false;

			const Handle<GameMap>& map = client->GetWorld()->GetMap();
			SPAssert(map);

			if (resting) {
				// check that the voxel under the casing still exists
				const IntVector3 restLp = matrix.GetOrigin().Floor();
				if (map->ClipWorld(restLp.x, restLp.y, static_cast<int>(groundZ))) {
					// smoothly lie flat (exponential approach, independent of the frame rate)
					matrix = AlignToSide(matrix, 1.0F - expf(-kSettleRate * dt));

					// keep the lowest point on top of the ground while it settles
					matrix = PlaceOnGround(matrix, box, scale, groundZ);
					return true;
				}

				// the support was destroyed: go back to the physics and fall
				resting = false;
				velocity = MakeVector3(0.0F, 0.0F, 0.0F);
			}

			const Matrix4 lastMat = matrix;

			// the lowest point of the model touches the ground, not the origin
			Vector3 lastLow = lastMat.GetOrigin();
			lastLow.z += LowestPointOffset(matrix, box, scale);

			matrix = matrix * Matrix4::Rotate(MakeVector3(1, 0, 0), rot.x * dt);
			matrix = matrix * Matrix4::Rotate(MakeVector3(0, 1, 0), rot.y * dt);
			matrix = Matrix4::Translate(velocity * dt) * matrix;
			velocity.z += dt * 32.0F;

			Vector3 low = matrix.GetOrigin();
			low.z += LowestPointOffset(matrix, box, scale);
			const IntVector3 lp = low.Floor();

			if (lp.z >= 63) { // dropped into water
				if (waterSound) {
					if (!client->IsMuted() && distSqr < CASING_BOUNCE_DIST_SQ) {
						IAudioDevice& dev = client->GetAudioDevice();
						AudioParam param;
						param.referenceDistance = 0.6F;
						param.pitch = 0.9F + SampleRandomFloat() * 0.2F;
						dev.Play(waterSound, lastMat.GetOrigin(), param);
					}
					waterSound = NULL;
				}

				if (cg_particles && distSqr < CASING_BOUNCE_DIST_SQ) {
					Handle<IImage> img = renderer.RegisterWhiteImage();

					Vector4 col = {1, 1, 1, 0.6F};
					Vector3 pt = matrix.GetOrigin();
					pt.z = 62.99F;

					int splats = SampleRandomInt(1, 3);
					for (int i = 0; i < splats; i++) {
						auto ent = stmp::make_unique<ParticleSpriteEntity>(*client, img, col);
						ent->SetTrajectory(pt,
							MakeVector3(
								SampleRandomFloat() - SampleRandomFloat(),
								SampleRandomFloat() - SampleRandomFloat(),
								-SampleRandomFloat()) * 2.0F, 1.0F, 0.4F
						);
						ent->SetRotation(SampleRandomFloat() * kTwoPi);
						ent->SetRadius(0.1F + SampleRandomFloat() * SampleRandomFloat() * 0.1F);
						ent->SetLifeTime(2.0F, 0.0F, 1.0F);
						client->AddLocalEntity(std::move(ent));
					}
				}

				return false;
			}

			if (map->ClipWorld(lp.x, lp.y, lp.z)) { // hit a wall
				const IntVector3 lp2 = lastLow.Floor();
				bool ground = false;
				if (lp.z != lp2.z &&
					((lp.x == lp2.x && lp.y == lp2.y) || !map->ClipWorld(lp.x, lp.y, lp2.z))) {
					velocity.z = -velocity.z;
					// it only counts as ground when the voxel under the column where the casing stays is solid
					if (lp2.z < lp.z && map->ClipWorld(lp2.x, lp2.y, lp.z)) { // ground hit
						ground = true;
						groundZ = static_cast<float>(lp.z); // top face of the solid voxel
						if (dropSound) {
							if (!client->IsMuted() && distSqr < CASING_BOUNCE_DIST_SQ) {
								IAudioDevice& dev = client->GetAudioDevice();
								AudioParam param;
								param.referenceDistance = 0.6F;
								dev.Play(dropSound, lastMat.GetOrigin(), param);
							}
							dropSound = NULL;
						}
					}
				} else if (lp.x != lp2.x &&
					((lp.y == lp2.y && lp.z == lp2.z) || !map->ClipWorld(lp2.x, lp.y, lp.z))) {
					velocity.x = -velocity.x;
				} else if (lp.y != lp2.y &&
					((lp.x == lp2.x && lp.z == lp2.z) || !map->ClipWorld(lp.x, lp2.y, lp.z))) {
					velocity.y = -velocity.y;
				} else {
					return false;
				}

				matrix = lastMat;
				velocity *= 0.46F; // lose some velocity due to friction

				/*
				// change the spin direction on every impact
				if (rot.x > 0.0F)
					rot.y = -rot.y;
				rot.x = -rot.x;
				*/

				if (ground) {
					// every ground bounce tips the casing more towards lying flat
					matrix = AlignToSide(matrix, kBounceCorrection);

					// put the lowest point right on top of the ground
					matrix = PlaceOnGround(matrix, box, scale, groundZ);

					// slow enough: stay there until it expires, the settling is done while resting
					if (velocity.GetSquaredLength() < 1.0F) {
						velocity = MakeVector3(0.0F, 0.0F, 0.0F);
						resting = true;
					}
				}
			}

			return true;
		}

		void GunCasing::Render3D() {
			// shrink during the last moments of the lifetime
			const float shrink = resting ? Clamp(time * kShrinkSpeed, 0.0F, 1.0F) : 1.0F;

			// the model shrinks around its origin, so lower it to keep the bottom on the ground
			const float drop = LowestPointOffset(matrix, box, scale) * (1.0F - shrink);

			ModelRenderParam param;
			param.matrix = Matrix4::Translate(0.0F, 0.0F, drop)
				* matrix * Matrix4::Scale(scale * shrink);
			renderer.RenderModel(*model, param);
		}
	} // namespace client
} // namespace spades
