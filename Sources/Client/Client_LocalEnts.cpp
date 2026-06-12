/*
 Copyright (c) 2013 yvt
 based on code of pysnip (c) Mathias Kaerlev 2011-2012.

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

#include <cmath>
#include <cstdlib>
#include <iterator>

#include "Client.h"

#include <Core/ConcurrentDispatch.h>
#include <Core/Settings.h>
#include <Core/Strings.h>

#include "IAudioChunk.h"
#include "IAudioDevice.h"

#include "BloodMarks.h"
#include "Corpse.h"
#include "ILocalEntity.h"
#include "ParticleSpriteEntity.h"
#include "SmokeSpriteEntity.h"

#include "GameMap.h"
#include "Weapon.h"
#include "World.h"

#include "NetClient.h"

DEFINE_SPADES_SETTING(cg_blood, "2");
DEFINE_SPADES_SETTING(cg_particles, "2");
DEFINE_SPADES_SETTING(cg_particlesBloodNum, "4");
DEFINE_SPADES_SETTING(cg_particlesGrenadeNum, "64");
DEFINE_SPADES_SETTING(cg_waterImpact, "1");

DEFINE_SPADES_SETTING(cg_corpseSoftLimit, "6");
DEFINE_SPADES_SETTING(cg_corpseHardLimit, "16");

SPADES_SETTING(cg_manualFocus);
DEFINE_SPADES_SETTING(cg_autoFocusSpeed, "0.4");

namespace spades {
	namespace client {

#pragma mark - Local Entities / Effects

		void Client::RemoveCorpses() {
			SPADES_MARK_FUNCTION();

			int numCorpses = static_cast<int>(corpses.size());
			if (numCorpses > (int)cg_corpseHardLimit) {
				if (corpses.front().get() == lastLocalCorpse)
					lastLocalCorpse = nullptr;
				corpses.pop_front();
			} else if (numCorpses > (int)cg_corpseSoftLimit) {
				RemoveInvisibleCorpses();
			}
		}

		void Client::RemoveInvisibleCorpses() {
			SPADES_MARK_FUNCTION();

			int numCorpses = static_cast<int>(corpses.size());
			int cnt = numCorpses - (int)cg_corpseSoftLimit;

			decltype(corpses)::iterator it;
			std::vector<decltype(it)> its;
			for (it = corpses.begin(); it != corpses.end(); it++) {
				if (cnt <= 0)
					break;

				auto& c = *it;
				if (c->IsVisibleFrom(lastSceneDef.viewOrigin))
					continue;

				if (c.get() == lastLocalCorpse)
					lastLocalCorpse = nullptr;
				its.push_back(it);
				cnt--;
			}

			for (const auto& it : its)
				corpses.erase(it);
		}

		void Client::RemoveCorpseForPlayer(int playerId) {
			for (auto it = corpses.begin(); it != corpses.end();) {
				auto cur = it;
				++it;

				auto& c = *cur;
				if (c->GetPlayerId() != playerId)
					continue;

				if (c.get() == lastLocalCorpse)
					lastLocalCorpse = nullptr;
				corpses.erase(cur);
			}
		}

		void Client::RemoveAllLocalEntities() {
			SPADES_MARK_FUNCTION();

			// remove all corpses
			lastLocalCorpse = nullptr;
			corpses.clear();

			damageIndicators.clear();
			localEntities.clear();
			
			if (bloodMarks)
				bloodMarks->Clear();
		}

		stmp::optional<std::tuple<Player&, hitTag_t>> Client::HotTrackedPlayer() {
			if (!IsFirstPerson(GetCameraMode()))
				return {};

			SPAssert(world);

			auto& camTarget = GetCameraTargetPlayer();

			Vector3 origin = camTarget.GetEye();
			Vector3 dir = camTarget.GetFront();
			World::WeaponRayCastResult res = world->WeaponRayCast(origin, dir, camTarget.GetId());

			if (res.hit == false || !res.playerId)
				return {};

			Player& p = world->GetPlayer(res.playerId.value()).value();

			// don't hot track enemies (non-spectator only)
			if (!camTarget.IsTeammate(p) && !camTarget.IsSpectator())
				return {};

			return std::tuple<Player&, hitTag_t>{p, res.hitFlag};
		}

		bool Client::IsMuted() {
			// prevent to play loud sound at connection
			// caused by saved packets
			return time - worldSetTime < 0.05F;
		}

		void Client::Bleed(spades::Vector3 pos, bool bounce) {
			SPADES_MARK_FUNCTION();

			if (!cg_blood)
				return;

			int particleLevel = cg_particles;
			if (!particleLevel)
				return;

			// distance cull
			float distSqr = (pos - lastSceneDef.viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			Vector3 velBias = {0, 0, -0.5F};

			// fragments
			Handle<IImage> img = renderer->RegisterImage("Gfx/White.tga");

			uint32_t col = IntVectorToColor(MakeIntVector3(127, 0, 0));
			col = map->GetColorJit(col); // jit the colour

			Vector4 color = ConvertColorRGBA(IntVectorFromColor(col));

			int particlesNum = cg_particlesBloodNum;
			for (int i = 0; i < particlesNum; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(pos, (RandomVector() + velBias * 0.5F) * 8.0F);
				ent->SetRadius(0.4F);
				ent->SetLifeTime(3.0F, 0.0F, 1.0F);
				if (distSqr < PARTICLE_BOUNCE_DIST_SQ || bounce)
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				localEntities.emplace_back(std::move(ent));
			}

			if (particleLevel < 2)
				return;

			color = MakeVector4(0.7F, 0.35F, 0.37F, 0.6F);
			for (int i = 0; i < 2; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 100.0F,
					SmokeSpriteEntity::Type::Explosion);
				ent->SetTrajectory(pos, RandomVector() * 0.7F, 0.8F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * M_PI_F * 2.0F);
				ent->SetRadius(0.5F + SampleRandomFloat() * SampleRandomFloat() * 0.2F, 2.0F);
				ent->SetLifeTime(0.2F + SampleRandomFloat() * 0.2F, 0.06F, 0.2F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			color.w *= 0.1F;
			{
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 40.0F);
				ent->SetTrajectory(pos, RandomVector() * 0.7F, 0.8F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * M_PI_F * 2.0F);
				ent->SetRadius(0.7F + SampleRandomFloat() * SampleRandomFloat() * 0.2F, 2.0F, 0.1F);
				ent->SetLifeTime(0.8F + SampleRandomFloat() * 0.4F, 0.06F, 1.0F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}
		}

		void Client::EmitBlockFragments(spades::Vector3 pos, IntVector3 col) {
			SPADES_MARK_FUNCTION();

			int particleLevel = cg_particles;
			if (!particleLevel)
				return;

			// distance cull
			float distSqr = (pos - lastSceneDef.viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			// determines free space around for SetTrajectory
			Vector3 velBias = {0, 0, 0};
			if (!map->ClipBox(pos.x, pos.y, pos.z)) {
				if (map->ClipBox(pos.x + 1.0F, pos.y, pos.z))
					velBias.x -= 1.0F;
				if (map->ClipBox(pos.x - 1.0F, pos.y, pos.z))
					velBias.x += 1.0F;
				if (map->ClipBox(pos.x, pos.y + 1.0F, pos.z))
					velBias.y -= 1.0F;
				if (map->ClipBox(pos.x, pos.y - 1.0F, pos.z))
					velBias.y += 1.0F;
				if (map->ClipBox(pos.x, pos.y, pos.z + 1.0F))
					velBias.z -= 1.0F;
				if (map->ClipBox(pos.x, pos.y, pos.z - 1.0F))
					velBias.z += 1.0F;
			}

			// fragments
			Handle<IImage> img = renderer->RegisterImage("Gfx/White.tga");

			Vector4 color = ConvertColorRGBA(col);

			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				Vector3 dir = RandomVector() + velBias * 0.5F;
				ent->SetTrajectory(pos + dir * 0.2F, dir * 8.0F);
				ent->SetRadius(0.4F);
				ent->SetLifeTime(3.0F, 0.0F, 1.0F);
				if (distSqr < PARTICLE_BOUNCE_DIST_SQ)
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				localEntities.emplace_back(std::move(ent));
			}

			if (particleLevel < 2)
				return;

			if (distSqr < 32.0F * 32.0F) {
				for (int i = 0; i < 8; i++) {
					auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
					ent->SetTrajectory(pos, RandomVector() * 12.0F, 1.0F, 0.9F);
					ent->SetRotation(SampleRandomFloat() * M_PI_F * 2.0F);
					ent->SetRadius(0.2F + SampleRandomFloat() * SampleRandomFloat() * 0.25F);
					ent->SetLifeTime(3.0F, 0.0F, 1.0F);
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
					localEntities.emplace_back(std::move(ent));
				}
			}

			color += (MakeVector4(1, 1, 1, 1) - color) * 0.2F;
			color.w *= 0.2F;
			for (int i = 0; i < 2; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 100.0F);
				ent->SetTrajectory(pos, RandomVector() * 0.7F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * M_PI_F * 2.0F);
				ent->SetRadius(0.6F + SampleRandomFloat() * SampleRandomFloat() * 0.2F, 0.8F);
				ent->SetLifeTime(0.3F + SampleRandomFloat() * 0.3F, 0.06F, 0.4F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}
		}

		void Client::EmitBlockDestroyFragments(spades::IntVector3 pos) {
			SPADES_MARK_FUNCTION();

			if (!cg_particles)
				return;

			Vector3 origin = MakeVector3(pos) + 0.5F;

			// distance cull
			float distSqr = (origin - lastSceneDef.viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			// fragments
			Handle<IImage> img = renderer->RegisterImage("Gfx/White.tga");

			uint32_t col = map->GetColor(pos.x, pos.y, pos.z);
			col = map->GetColorJit(col); // jit the colour
			Vector4 color = ConvertColorRGBA(IntVectorFromColor(col));

			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(origin, RandomVector() * 8.0F);
				ent->SetRadius(0.4F);
				ent->SetLifeTime(3.0F, 0.0F, 1.0F);
				if (distSqr < PARTICLE_BOUNCE_DIST_SQ)
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				localEntities.emplace_back(std::move(ent));
			}
		}

		void Client::MuzzleFire(spades::Vector3 pos, bool smoke) {
			// distance cull
			float distSqr = (pos - lastSceneDef.viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			DynamicLightParam l;
			l.type = DynamicLightTypePoint;
			l.origin = pos;
			l.radius = 5.0F;
			l.color = MakeVector3(3.0F, 1.6F, 0.5F);
			flashDlights.push_back(l);

			if ((int)cg_particles < 2 || !smoke)
				return;

			Vector3 velBias = {0, 0, -0.5F};
			Vector4 color = MakeVector4(0.8F, 0.8F, 0.8F, 0.3F);

			// rapid smoke
			for (int i = 0; i < 2; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 120.0F,
					SmokeSpriteEntity::Type::Explosion);
				ent->SetTrajectory(pos, (RandomVector() + velBias * 0.5F) * 0.3F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * M_PI_F * 2.0F);
				ent->SetRadius(0.4F, 3.0F, 0.0000005F);
				ent->SetLifeTime(0.2F + SampleRandomFloat() * 0.1F, 0.0F, 0.3F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			// fire smoke
			color = MakeVector4(1.0F, 0.6F, 0.4F, 0.2F) * 5.0F;
			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 120.0F,
					SmokeSpriteEntity::Type::Explosion);
				ent->SetTrajectory(pos, (RandomVector() + velBias * 0.5F) * 0.3F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * M_PI_F * 2.0F);
				ent->SetRadius(0.2F + SampleRandomFloat() * SampleRandomFloat() * 0.3F, 3.0F, 0.0000005F);
				ent->SetLifeTime(0.01F + SampleRandomFloat() * 0.02F, 0.0F, 0.01F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}
		}

		void Client::KickCamera(float strength) {
			grenadeVibration = std::min(grenadeVibration + strength, 0.4F);
			grenadeVibrationSlow = std::min(grenadeVibrationSlow + strength * 5.0F, 0.4F);
		}

		void Client::GrenadeExplosion(spades::Vector3 pos) {
			// distance cull
			float dist = (pos - lastSceneDef.viewOrigin).GetLength();
			if (dist > FOG_DISTANCE + 16.0F)
				return;

			DynamicLightParam l;
			l.type = DynamicLightTypePoint;
			l.origin = pos;
			l.radius = 16.0F;
			l.color = MakeVector3(3.0F, 1.6F, 0.5F);
			l.useLensFlare = true;
			flashDlights.push_back(l);

			if (dist <= FOG_DISTANCE)
				KickCamera(2.0F / (dist + 5.0F));

			int particleLevel = cg_particles;
			if (!particleLevel)
				return;

			// determines free space around for SetTrajectory
			Vector3 velBias = {0, 0, 0};
			if (!map->ClipBox(pos.x, pos.y, pos.z)) {
				if (map->ClipBox(pos.x + 1.0F, pos.y, pos.z))
					velBias.x -= 1.0F;
				if (map->ClipBox(pos.x - 1.0F, pos.y, pos.z))
					velBias.x += 1.0F;
				if (map->ClipBox(pos.x, pos.y + 1.0F, pos.z))
					velBias.y -= 1.0F;
				if (map->ClipBox(pos.x, pos.y - 1.0F, pos.z))
					velBias.y += 1.0F;
				if (map->ClipBox(pos.x, pos.y, pos.z + 1.0F))
					velBias.z -= 1.0F;
				if (map->ClipBox(pos.x, pos.y, pos.z - 1.0F))
					velBias.z += 1.0F;
			}

			// fragments
			Handle<IImage> img = renderer->RegisterImage("Gfx/White.tga");

			uint32_t col = IntVectorToColor(MakeIntVector3(70, 70, 70));
			col = map->GetColorJit(col); // jit the colour
			Vector4 color = ConvertColorRGBA(IntVectorFromColor(col));

			int particlesNum = cg_particlesGrenadeNum;
			for (int i = 0; i < particlesNum; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				Vector3 dir = RandomUnitVector() + velBias * 0.5F;
				float radius = 0.3F + SampleRandomFloat() * SampleRandomFloat() * 0.3F;
				ent->SetTrajectory(pos + dir * 0.2F, dir * 16.0F, 0.1F + radius * 3.0F);
				ent->SetRadius(radius);
				ent->SetLifeTime(3.5F + SampleRandomFloat() * 2.0F, 0.0F, 1.0F);
				if (dist < PARTICLE_BOUNCE_DIST)
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				localEntities.emplace_back(std::move(ent));
			}

			if (particleLevel < 2)
				return;

			// rapid smoke
			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 60.0F,
					SmokeSpriteEntity::Type::Explosion);
				ent->SetTrajectory(pos, (RandomUnitVector() + velBias * 0.5F) * 2.0F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * M_PI_F * 2.0F);
				ent->SetRadius(0.6F + SampleRandomFloat() * SampleRandomFloat() * 0.4F, 2.0F, 0.2F);
				ent->SetLifeTime(1.8F + SampleRandomFloat() * 0.1F, 0.0F, 0.2F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			// slow smoke
			color.w = 0.25F;
			for (int i = 0; i < 8; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 30.0F);
				ent->SetTrajectory(pos, (MakeVector3(SampleRandomFloat() - SampleRandomFloat(),
							   SampleRandomFloat() - SampleRandomFloat(),
							   (SampleRandomFloat() - SampleRandomFloat()) * 0.2F)) * 2.0F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * M_PI_F * 2.0F);
				ent->SetRadius(1.5F + SampleRandomFloat() * SampleRandomFloat() * 0.8F, 0.2F);
				switch (particleLevel) {
					case 1: ent->SetLifeTime(0.8F + SampleRandomFloat() * 1.0F, 0.1F, 8.0F); break;
					case 2: ent->SetLifeTime(1.5F + SampleRandomFloat() * 2.0F, 0.1F, 8.0F); break;
					case 3:
					default: ent->SetLifeTime(2.0F + SampleRandomFloat() * 5.0F, 0.1F, 8.0F); break;
				}
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			// fire smoke
			color = MakeVector4(1, 0.7F, 0.4F, 0.2F) * 5.0F;
			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 120.0F,
					SmokeSpriteEntity::Type::Explosion);
				ent->SetTrajectory(pos, (RandomUnitVector() + velBias) * 6.0F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * M_PI_F * 2.0F);
				ent->SetRadius(0.3F + SampleRandomFloat() * SampleRandomFloat() * 0.4F, 3.0F, 0.1F);
				ent->SetLifeTime(0.18F + SampleRandomFloat() * 0.03F, 0.0F, 0.1F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}
		}

		void Client::GrenadeExplosionUnderwater(spades::Vector3 pos) {
			// distance cull
			float dist = (pos - lastSceneDef.viewOrigin).GetLength();
			if (dist > FOG_DISTANCE + 16.0F)
				return;

			if (dist <= FOG_DISTANCE)
				KickCamera(1.0F / (dist + 5.0F));

			int particleLevel = cg_particles;
			if (!particleLevel)
				return;

			Vector3 velBias = {0, 0, -1.0F};

			// fragments
			Handle<IImage> img = renderer->RegisterImage("Gfx/White.tga");

			Vector4 color = MakeVector4(1, 1, 1, 0.6F);

			int particlesNum = cg_particlesGrenadeNum;
			for (int i = 0; i < particlesNum; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				Vector3 dir = RandomUnitVector() + velBias * 0.5F;
				float radius = 0.3F + SampleRandomFloat() * SampleRandomFloat() * 0.3F;
				ent->SetTrajectory(pos + dir * 0.2F, dir * 16.0F, 0.1F + radius * 3.0F);
				ent->SetRadius(radius);
				ent->SetLifeTime(3.5F + SampleRandomFloat() * 2.0F, 0.0F, 1.0F);
				if (dist < PARTICLE_BOUNCE_DIST)
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				localEntities.emplace_back(std::move(ent));
			}

			if (particleLevel < 2)
				return;

			// water1
			img = renderer->RegisterImage("Textures/WaterExpl.png");
			color = MakeVector4(0.95F, 0.95F, 0.95F, 0.6F);
			for (int i = 0; i < 7; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(pos, (MakeVector3(0.0F, 0.0F, -SampleRandomFloat() * 7.0F)) * 2.5F, 0.3F);
				ent->SetRadius(1.2F + SampleRandomFloat() * SampleRandomFloat() * 0.4F, 0.6F);
				ent->SetLifeTime(2.0F + SampleRandomFloat() * 0.3F, 0.1F, 0.2F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			// water2
			img = renderer->RegisterImage("Textures/Fluid.png");
			color.w = 0.9F;
			for (int i = 0; i < 16; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(pos, (MakeVector3(SampleRandomFloat() - SampleRandomFloat(),
													 SampleRandomFloat() - SampleRandomFloat(),
													 -SampleRandomFloat() * 7.0F)) * 3.5F);
				ent->SetRotation(SampleRandomFloat() * M_PI_F * 2.0F);
				ent->SetRadius(0.6F + SampleRandomFloat() * SampleRandomFloat() * 0.3F, 0.5F);
				ent->SetLifeTime(2.0F + SampleRandomFloat() * 0.3F, 0.1F, 0.2F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			// slow smoke
			color.w = 0.3F;
			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<SmokeSpriteEntity>(*this, color, 10.0F);
				ent->SetTrajectory(pos, (MakeVector3(SampleRandomFloat() - SampleRandomFloat(),
							   SampleRandomFloat() - SampleRandomFloat(),
							   (SampleRandomFloat() - SampleRandomFloat()) * 0.2F)) * 2.0F, 1.0F, 0.0F);
				ent->SetRotation(SampleRandomFloat() * M_PI_F * 2.0F);
				ent->SetRadius(1.5F + SampleRandomFloat() * SampleRandomFloat() * 0.6F, 0.2F);
				ent->SetLifeTime(2.0F + SampleRandomFloat() * 0.3F, 0.2F, 1.5F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				localEntities.emplace_back(std::move(ent));
			}

			// TODO: wave?
		}

		void Client::BulletHitWaterSurface(spades::Vector3 pos) {
			int particleLevel = cg_particles;
			if (!particleLevel)
				return;

			// distance cull
			float distSqr = (pos - lastSceneDef.viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			Vector3 velBias = {0, 0, -1.0F};

			// fragments
			Handle<IImage> img = renderer->RegisterImage("Gfx/White.tga");

			Vector4 color = MakeVector4(1, 1, 1, 0.6F);

			for (int i = 0; i < 4; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(pos, (RandomVector() + velBias * 0.5F) * 8.0F);
				ent->SetRadius(0.3F);
				ent->SetLifeTime(3.0F, 0.0F, 1.0F);
				if (distSqr < PARTICLE_BOUNCE_DIST_SQ)
					ent->SetBlockHitAction(BlockHitAction::BounceWeak);
				localEntities.emplace_back(std::move(ent));
			}

			if (particleLevel < 2 || !cg_waterImpact)
				return;

			// water1
			img = renderer->RegisterImage("Textures/WaterExpl.png");
			color = MakeVector4(0.95F, 0.95F, 0.95F, 0.3F);
			for (int i = 0; i < 2; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(pos, (MakeVector3(SampleRandomFloat() - SampleRandomFloat(),
												SampleRandomFloat() - SampleRandomFloat(),
												-SampleRandomFloat() * 7.0F)), 0.3F, 0.6F);
				ent->SetRadius(0.6F + SampleRandomFloat() * SampleRandomFloat() * 0.4F, 0.7F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				ent->SetLifeTime(3.0F + SampleRandomFloat() * 0.3F, 0.1F, 0.6F);
				localEntities.emplace_back(std::move(ent));
			}

			// water2
			img = renderer->RegisterImage("Textures/Fluid.png");
			color.w = 0.9F;
			for (int i = 0; i < 6; i++) {
				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(pos, (MakeVector3(SampleRandomFloat() - SampleRandomFloat(),
												SampleRandomFloat() - SampleRandomFloat(),
												-SampleRandomFloat() * 16.0F)));
				ent->SetRotation(SampleRandomFloat() * M_PI_F * 2.0F);
				ent->SetRadius(0.6F + SampleRandomFloat() * SampleRandomFloat() * 0.6F, 0.6F);
				ent->SetBlockHitAction(BlockHitAction::Ignore);
				ent->SetLifeTime(3.0F + SampleRandomFloat() * 0.3F, SampleRandomFloat() * 0.3F, 0.6F);
				localEntities.emplace_back(std::move(ent));
			}

			// TODO: wave?
		}

		void Client::EmitSnowflakes(spades::Vector3 pos) {
			SPADES_MARK_FUNCTION();

			if (time - lastSnowDropTime < 0.2F)
				return;

			// fragments
			Handle<IImage> img = renderer->RegisterImage("Gfx/White.tga");

			Vector4 color = MakeVector4(1, 1, 1, 0.9F);

			const float spawnRange = 140.0F;
			const float spawnHeight = 64.0F;
			const int particlesNum = 20;

			for (int i = 0; i < particlesNum; ++i) {
				Vector3 spawnPos;
				spawnPos.x = pos.x + (SampleRandomFloat() * 2.0F - 1.0F) * spawnRange;
				spawnPos.y = pos.y + (SampleRandomFloat() * 2.0F - 1.0F) * spawnRange;
				spawnPos.z = pos.z - spawnHeight;

				Vector3 vel = RandomVector() * 8.0F;
				vel.z = Mix(0.5F, 1.0F, SampleRandomFloat());

				auto ent = stmp::make_unique<ParticleSpriteEntity>(*this, img, color);
				ent->SetTrajectory(spawnPos, vel, 0.8F, 0.1F);
				ent->SetRadius(0.3F + SampleRandomFloat() * SampleRandomFloat() * 0.2F);
				ent->SetLifeTime(10.0F, 0.5F, 1.0F);
				ent->SetBlockHitAction(BlockHitAction::Stick);
				localEntities.emplace_back(std::move(ent));
			}

			lastSnowDropTime = time;
		}

#pragma mark - Camera Control

		enum { AutoFocusPoints = 4 };
		void Client::UpdateAutoFocus(float dt) {
			if (world && autoFocusEnabled && cg_manualFocus) {
				// Compute focal length
				float measureRange = tanf(lastSceneDef.fovY * 0.5F) * 0.2F;
				const Vector3 camPos = lastSceneDef.viewOrigin;
				const Vector3 camDir = lastSceneDef.viewAxis[2];
				const Vector3 camX = lastSceneDef.viewAxis[0].Normalize() * measureRange;
				const Vector3 camY = lastSceneDef.viewAxis[1].Normalize() * measureRange;

				float distances[AutoFocusPoints * AutoFocusPoints];
				std::size_t numValidDistances = 0;
				Vector3 camDir1 = camDir.Normalize() - camX - camY;
				const Vector3 camDX = camX * (2.0F / (AutoFocusPoints - 1));
				const Vector3 camDY = camY * (2.0F / (AutoFocusPoints - 1));
				for (int x = 0; x < AutoFocusPoints; ++x) {
					Vector3 camDir2 = camDir1;
					for (int y = 0; y < AutoFocusPoints; ++y) {
						float dist = RayCastForAutoFocus(camPos, camDir2);
						dist *= (1.0F / camDir.GetLength());

						if (std::isfinite(dist) && dist > 0.8F)
							distances[numValidDistances++] = dist;

						camDir2 += camDY;
					}

					camDir1 += camDX;
				}

				if (numValidDistances > 0) {
					// Take median
					std::sort(distances, distances + numValidDistances);

					float dist = (numValidDistances & 1)
								   ? distances[numValidDistances >> 1]
								   : (distances[numValidDistances >> 1] +
									  distances[(numValidDistances >> 1) - 1]) * 0.5F;

					targetFocalLength = dist;
				}
			}

			// Change the actual focal length slowly
			{
				float dist = 1.0F / targetFocalLength;
				float curDist = 1.0F / focalLength;
				const float maxSpeed = cg_autoFocusSpeed;

				if (dist > curDist)
					curDist = std::min(dist, curDist + maxSpeed * dt);
				else
					curDist = std::max(dist, curDist - maxSpeed * dt);

				focalLength = 1.0F / curDist;
			}
		}
		float Client::RayCastForAutoFocus(const Vector3& origin, const Vector3& direction) {
			SPAssert(world);

			World::WeaponRayCastResult res = world->WeaponRayCast(origin, direction, {});
			if (res.hit)
				return Vector3::Dot(res.hitPos - origin, lastSceneDef.viewAxis[2]);

			return NAN;
		}
	} // namespace client
} // namespace spades