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

#include "RocketEntity.h"
#include "IRenderer.h"
#include "IModel.h"
#include "Client.h"
#include "ParticleSpriteEntity.h"
#include "SmokeSpriteEntity.h"
#include "GameMap.h"
#include "World.h"
#include "IAudioDevice.h"
#include "IAudioChunk.h"
#include <Core/Settings.h>

// Live-tunable orientation fixes (degrees), applied in the MODEL's local
// frame in Render3D, i.e. constant corrections valid for every flight
// direction. Adjust in the in-game console without recompiling, then
// freeze the winning values here.
//   cg_rocketPitchFix : tilt the nose around local X
//   cg_rocketYawFix   : spin in the model plane (around local Z)
//   cg_rocketRollFix  : roll around the flight axis (local Y)
// Rocket.kv6 warhead is along -Y (community model; CONFIRMED in game,
// 8 Nov 2026: cg_rocketPitchFix 180 makes the rocket fly nose-first).
// Frozen as the default below. If the fins look upside-down in flight,
// add roll 180 (pitch 180 flips the local Z axis).
DEFINE_SPADES_SETTING(cg_rocketYawFix, "0");
DEFINE_SPADES_SETTING(cg_rocketPitchFix, "180");
DEFINE_SPADES_SETTING(cg_rocketRollFix, "0");

// Flight "woosh" loop volume. DEFINE lives ONLY here (RocketEntity.cpp
// is the only file that reads this CVar; a duplicate DEFINE anywhere
// else = linker error).
DEFINE_SPADES_SETTING(cg_rocketWooshVolume, "1");

// Detonation boom tuning (LIVE, console, no rebuild):
//   cg_rocketBoomVolume : multiplies the manual 2D falloff of the
//     detonation boom. 1 = old volume, 1.5 = +50% (masking fix,
//     Nov 2026: the fire sample used to mask the close-range boom;
//     the sample was shortened, this boosts what remains).
//   cg_rocketBoomPitch  : 1 = normal; 0.8 = deeper boom, separates
//     it spectrally from the fire crack (less masking). Freeze the
//     winning values in the defaults below.
DEFINE_SPADES_SETTING(cg_rocketBoomVolume, "1.5");
DEFINE_SPADES_SETTING(cg_rocketBoomPitch, "0.85");

#include <cstdlib>

namespace spades {
	namespace client {

		// Rodrigues' rotation: rotate vector v around unit axis k by angle
		static Vector3 RotateAroundAxis(Vector3 v, Vector3 k, float angle) {
			float c = cosf(angle);
			float s = sinf(angle);
			return v * c + Vector3::Cross(k, v) * s +
			       k * Vector3::Dot(k, v) * (1.f - c);
		}

		// detonation flash light tunables (RocketEntity's equivalents
		// are private there; keep the two in sync)
		static constexpr float kFlashRadius = 6.0f; // 4x rifle bullet flash
		static constexpr float kFlashTime = 0.5f;    // s, decaying

		RocketEntity::RocketEntity(Client &cli, IAudioDevice *audio,
		                           Handle<IAudioChunk> woosh,
		                           Vector3 muzzlePos, Vector3 dir)
		    : client(cli), audioDevice(audio), wooshChunk(std::move(woosh)),
		      position(muzzlePos), direction(dir.Normalize()) {
			SPADES_MARK_FUNCTION();

			// first "woosh" right at launch, then Update re-triggers
			// it every WooshInterval (= exact duration of the sample)
			if (audioDevice && wooshChunk) {
				AudioParam param;
				param.volume = (float)cg_rocketWooshVolume;
				audioDevice->Play(wooshChunk.GetPointerOrNull(),
				                  position, param);
				wooshTimer = WooshInterval;
			}

			// grenade blast sample for the detonation boom
			// (played once by RocketFlashEntity at the detonation)
			if (audioDevice)
				explodeChunk = audioDevice->RegisterSound(
				  "Sounds/Weapons/Grenade/Explode1.opus");

			auto *r = &client.GetRenderer();
			rocketModel = r->RegisterModel("Models/Weapons/Grenade/Rocket.kv6");

			// map for block collision
			if (client.GetWorld())
				map = client.GetWorld()->GetMap();

			// per-rocket drift: total angle between 5 and 20 degrees,
			// around a fixed random axis perpendicular to the launch dir
			baseDirection = direction;
			deviationTotal = (DeviationMinAngle +
			                  SampleRandomFloat() * (DeviationMaxAngle - DeviationMinAngle)) *
			                 (float)kPi / 180.f;
			Vector3 rnd = RandomUnitVector();
			driftAxis = Vector3::Cross(direction, rnd).Normalize();
		}

		RocketEntity::~RocketEntity() { SPADES_MARK_FUNCTION(); }

		void RocketEntity::Preload(IRenderer *r) {
			// models will register on first use otherwise
			r->RegisterModel("Models/Weapons/Grenade/Rocket.kv6");
		}

		void RocketEntity::SpawnExplosionAt(Vector3 pos) {
			SPADES_MARK_FUNCTION();

			// explosion smoke: wider and denser than the grenade's, and it
			// lasts 3x longer (ExplosionLifeTime)
			for (int i = 0; i < 8; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(
				  client, MakeVector4(0.9f, 0.9f, 0.9f, 1.0f), 60.0f,
				  SmokeSpriteEntity::Type::Explosion);
				ent->SetTrajectory(pos, RandomUnitVector() * 1.5f, 1.0f, 0.0f);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				// burst clouds +50% (8 Nov 2026)
				ent->SetRadius(1.8f + SampleRandomFloat() * SampleRandomFloat() * 0.9f,
				               4.5f, 0.2f);
				ent->SetLifeTime(ExplosionLifeTime + SampleRandomFloat() * 0.4f,
				                 0.0f, 1.5f);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				client.AddLocalEntity(std::move(ent));
			}

			// bright detonation flash: 4x a rifle bullet flash,
			// decaying over ExplosionLightTime. Also plays the
			// detonation boom (hosted here because RocketEntity is
			// removed at the explosion).
			auto flash = stmp::make_unique<RocketFlashEntity>(
			  client, audioDevice, explodeChunk, pos);
			client.AddLocalEntity(std::move(flash));

			// lingering dark-gray smoke at the impact point: ~2.5 s,
			// as wide as the impact zone, rising ~7 units total
			// (DarkSmokeRiseSpeed * DarkSmokeLifeTime). z is
			// inverted: rise speed negative, gravScale 0 so the
			// engine's particle gravity does not pull the puffs back
			// down.
			for (int i = 0; i < DarkSmokeCount; i++) {
				Vector3 off = RandomUnitVector();
				off.z = 0.f;
				auto ent = stmp::make_unique<SmokeSpriteEntity>(
				  client, MakeVector4(0.18f, 0.18f, 0.18f, 0.95f), 60.0f,
				  SmokeSpriteEntity::Type::Steady);
				ent->SetTrajectory(pos + off * 0.4f,
				  MakeVector3(0.f, 0.f, -DarkSmokeRiseSpeed), 1.0f, 0.0f);
				ent->SetRotation(SampleRandomFloat() * kTwoPi);
				ent->SetRadius(
				  DarkSmokeRadius + SampleRandomFloat() * 0.5f, 0.12f);
				ent->SetLifeTime(
				  DarkSmokeLifeTime + SampleRandomFloat() * 0.5f,
				  0.3f, 0.8f);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				client.AddLocalEntity(std::move(ent));
			}
		}

		bool RocketEntity::Update(float dt) {
			SPADES_MARK_FUNCTION();

			age += dt;

			// --- flight "woosh" loop: Play returns no handle (the
			//     device has no stop API), so the short sample is
			//     re-triggered at its EXACT duration while the rocket
			//     flies. On explosion Update returns false and the
			//     loop simply stops re-triggering: the last chunk tail
			//     (at most WooshInterval, keep the sample short) is
			//     masked by the explosion sound.
			wooshTimer -= dt;
			if (audioDevice && wooshChunk && wooshTimer <= 0.f) {
				AudioParam param;
				param.volume = (float)cg_rocketWooshVolume;
				audioDevice->Play(wooshChunk.GetPointerOrNull(),
				                  position, param);
				wooshTimer = WooshInterval;
			}

			// --- flight profile: precise up to 150 blocks, then a
			//     light gradual drift (2-8 deg total, fixed per rocket) ---
			if (traveled > DeviationAfterDistance) {
				float per = Clamp((traveled - DeviationAfterDistance) /
				                    (MaxRange - DeviationAfterDistance),
				                  0.0f, 1.0f);
				direction = RotateAroundAxis(baseDirection, driftAxis,
				                             deviationTotal * per);
			}

			// --- move: constant speed along direction + slight gravity
			//     drop (~0.4 blocks at 150 blocks, Speed = 60) ---
			Vector3 prevPos = position;
			Vector3 vel = direction * Speed;
			vel.z -= Gravity * age;
			position += vel * dt;

			Vector3 stepVec = position - prevPos;
			float step = stepVec.GetLength();
			traveled += step;

			// --- block collision: burst where the rocket hits ---
			// Two fixes (18 Nov 2026, "1 in 5-7 rockets tunnel through
			// walls" bug):
			// (1) GENEROUS SCAN: CastRay2's DDA crosses ONE grid plane
			//     per iteration, so a diagonal step of length 1.73
			//     needs up to 3 iterations ((int)step + 1 = 2 was
			//     skipping the entry voxel on oblique hits).
			// (2) NO TUNNELING: startSolid (rocket inside a solid
			//     voxel: point-blank spawn, or an entry voxel the scan
			//     skipped last frame) now detonates right there.
			//     Previously `hit && !startSolid` suppressed EVERY
			//     frame while inside the wall: one missed entry = a
			//     rocket that flew through the whole wall.
			if (map && step > 0.f) {
				GameMap::RayCastResult res =
				  map->CastRay2(prevPos, stepVec / step,
				                (int)(step * 2.f) + 4);
				if (res.hit && !res.startSolid) {
					SpawnExplosionAt(res.hitPos);
					return false;
				}
				if (res.startSolid) {
					// inside a solid voxel: never tunnel through
					SpawnExplosionAt(position);
					return false;
				}
			}

			// --- air burst at max range (300 blocks) if nothing was hit ---
			if (traveled >= MaxRange) {
				SpawnExplosionAt(position);
				return false;
			}

			// --- smoke trail puffs (SmokeSpriteEntity, engine-side:
			//     no sandbox, drawn anywhere on the map). Wide, opaque
			//     puffs replace the removed Flame.kv6 exhaust model.
			//     Near the shooter the puffs are shifted to the RIGHT of
			//     the flight axis (view convention: +X is left, so the
			//     shooter's right is -X: lateral = up x direction),
			//     fading to zero after ~30 units, so the trail stops
			//     blocking the view right after launch.
			smokeTimer -= dt;
			if (smokeTimer <= 0.f && age < SmokeInterval * 60.f) {
				Vector3 lateral =
				  Vector3::Cross(MakeVector3(0.f, 0.f, 1.f), direction).Normalize();
				float nearBias = Clamp(1.f - traveled / 30.f, 0.f, 1.f);
				Vector3 puffPos = position - direction * 0.5f +
				                  lateral * 0.6f * nearBias;
				auto ent = stmp::make_unique<SmokeSpriteEntity>(
				  client, MakeVector4(0.85f, 0.85f, 0.85f, 0.95f), 100.f,
				  SmokeSpriteEntity::Type::Steady);
				ent->SetTrajectory(puffPos,
				                    MakeVector3(0.f, 0.f, 0.05f), 0.9f, 0.f);
				ent->SetRadius(SmokeRadius, SmokeGrowth);
				ent->SetLifeTime(SmokeLifeTime, 0.05f, 0.35f); // fast fade in/out
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				client.AddLocalEntity(std::move(ent));
				smokeTimer = SmokeInterval;
			}

			// hard safety cap: full flight (300 blocks at 60 u/s) lasts ~5 s
			return age < 60.f;
		}

		void RocketEntity::Render3D() {
			SPADES_MARK_FUNCTION();

			auto *r = &client.GetRenderer();

			// Build an orthonormal basis directly from the flight
			// direction. This is EXACT for every launch angle, unlike
			// the previous yaw/pitch Euler decomposition which mixed a
			// wrong yaw sign (atan2(x,y) instead of atan2(-x,y)) with a
			// bogus corrective pitch (kPi - pitch): those two errors
			// happened to cancel only for some directions, so the
			// model orientation changed with the firing angle.
			// (Conventions verified in Sources/Core/Math.{h,cpp}: Matrix4
			// is column-major, mat * v transforms points, FromAxis(a1,
			// a2, a3, origin) sets a1/a2/a3 as the X/Y/Z axis images.)
			//
			// Model frame assumption: Rocket.kv6 nose points along +Y.
			// The two Fix CVars below are applied in the MODEL's local
			// frame (before the basis), so they are constant corrections
			// valid for every flight direction:
			//   cg_rocketPitchFix : tilt nose around local X (180 = fly
			//                       tail-first if the kv6 nose is -Y)
			//   cg_rocketYawFix   : spin around local Z (model plane)
			//   cg_rocketRollFix  : roll around the flight axis (local Y)
			Vector3 fwd = direction; // model +Y is mapped onto this
			Vector3 ref = MakeVector3(0.f, 0.f, 1.f); // world up
			if (fabsf(fwd.z) > 0.999f)
				ref = MakeVector3(0.f, 1.f, 0.f);    // avoid degenerate cross
			Vector3 right = Vector3::Cross(fwd, ref).Normalize();
			Vector3 up = Vector3::Cross(right, fwd).Normalize();

			float d2r = (float)kPi / 180.f;
			// NOTE: Settings::ItemHandle exposes operator float(), not
			// a FloatValue member (checked in Sources/Core/Settings.h).
			Matrix4 mat = Matrix4::FromAxis(right, fwd, up, position) *
			              Matrix4::Rotate(MakeVector3(1, 0, 0),
			                              (float)cg_rocketPitchFix * d2r) *
			              Matrix4::Rotate(MakeVector3(0, 0, 1),
			                              (float)cg_rocketYawFix * d2r) *
			              Matrix4::Rotate(MakeVector3(0, 1, 0),
			                              (float)cg_rocketRollFix * d2r) *
			              Matrix4::Scale(0.05f);
			ModelRenderParam p;
			p.matrix = mat;
			r->RenderModel(*rocketModel, p);

			// glow along the flight: 2x a rifle bullet flash,
			// warm exhaust orange (8 Nov 2026)
			DynamicLightParam light;
			light.type = DynamicLightTypePoint;
			light.origin = position;
			light.radius = FlightLightRadius;
			light.color = MakeVector3(1.0f, 0.7f, 0.35f);
			r->AddLight(light);
		}

		RocketFlashEntity::RocketFlashEntity(Client &cli, IAudioDevice *audio,
		                                     Handle<IAudioChunk> chunk,
		                                     Vector3 pos)
		    : client(cli), audioDevice(audio), origin(pos) {
			// SINGLE detonation boom (simplified from the double
			// blast, 15 Nov 2026), played in 2D (PlayLocal):
			// no position, so none of the spatial machinery of the
			// fork applies. Debug history (15 Nov 2026): the mono
			// chunk alone played spatially was NEVER audible (even at
			// volume 6, matching the engine's own GrenadeExplosion
			// params), the mono + stereo pair of the engine was
			// muffled/faint close range and inaudible
			// past ~15-20 blocks, while the same chunk in 2D
			// (PlayLocal) was clearly audible. Suspect: the fork's
			// spatial path (EAX obstruction filter et al.) kills
			// sounds emitted at a block surface. So: 2D boom with a
			// MANUAL distance falloff, which keeps far explosions
			// audible while still quieter than close ones:
			//   volume = cg_rocketBoomVolume * BlastVolume / (1 + dist/12)
			//     0 blk -> 9.0 | 10 blk -> 4.9 | 30 blk -> 2.6
			//    60 blk -> 1.5 | 100 blk -> 0.94   (at boost 1.5)
			// Played right at the impact: the 0.25 s delay variant
			// was tested and REMOVED (15 Nov 2026) once the
			// fire sample was shortened - masking is fixed at the
			// sample level, not by delaying the boom.
			if (audioDevice && chunk) {
				float dist = 1.f;
				if (client.GetWorld()) {
					auto lp = client.GetWorld()->GetLocalPlayer();
					if (lp)
						dist = (origin - lp->GetEye()).GetLength();
				}
				AudioParam param;
				param.volume = (float)cg_rocketBoomVolume * BlastVolume /
				               (1.f + dist / 12.f);
				param.pitch = (float)cg_rocketBoomPitch;
				audioDevice->PlayLocal(chunk.GetPointerOrNull(), param);
			}
		}

		bool RocketFlashEntity::Update(float dt) {
			SPADES_MARK_FUNCTION();
			age += dt;
			return age < kFlashTime;
		}

		void RocketFlashEntity::Render3D() {
			SPADES_MARK_FUNCTION();
			auto *r = &client.GetRenderer();
			float per = Clamp(age / kFlashTime, 0.f, 1.f);
			DynamicLightParam light;
			light.type = DynamicLightTypePoint;
			light.origin = origin;
			// 4x a rifle bullet flash, decaying to nothing
			light.radius = kFlashRadius * (1.f - per);
			light.color = MakeVector3(1.0f, 0.75f, 0.4f);
			r->AddLight(light);
		}
	} // namespace client
} // namespace spades