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
#include <deque>

#include "GameMap.h"
#include "GameMapWrapper.h"
#include "GameProperties.h"
#include "Grenade.h"
#include "HitTestDebugger.h"
#include "IGameMode.h"
#include "IWorldListener.h"
#include "Player.h"
#include "Weapon.h"
#include "World.h"
#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/Settings.h>

DEFINE_SPADES_SETTING(cg_debugHitTest, "0");

SPADES_SETTING(cg_orientationSmoothing);

namespace spades {
	namespace client {

		World::World(const std::shared_ptr<GameProperties>& gameProperties)
			: gameProperties{gameProperties} {
			SPADES_MARK_FUNCTION();
		}
		World::~World() { SPADES_MARK_FUNCTION(); }

		size_t World::GetNumPlayers() {
			size_t numPlayers = 0;
			for (const auto& p : players) {
				if (p)
					++numPlayers;
			}
			return numPlayers;
		}

		void World::UpdatePlayers(float dt) {
			for (const auto& p : players) {
				if (p && !p->IsSpectator())
					p->Update(dt);
			}
		}

		void World::UpdatePlayersSmooth(float dt) {
			for (const auto& p : players) {
				if (p && !p->IsSpectator())
					p->UpdateSmooth(dt);
			}
		}

		void World::Advance(float dt) {
			SPADES_MARK_FUNCTION();

			ApplyBlockActions();

			UpdatePlayers(dt);

			while (!damagedBlocksQueue.empty()) {
				auto it = damagedBlocksQueue.begin();
				if (it->first > time)
					break;

				const auto& pos = it->second;
				if (map && map->IsSolid(pos.x, pos.y, pos.z)) {
					uint32_t col = map->GetColor(pos.x, pos.y, pos.z);
					col = (col & 0xFFFFFF) | (100UL << 24);
					map->Set(pos.x, pos.y, pos.z, true, col);
				}

				damagedBlocksQueueMap.erase(damagedBlocksQueueMap.find(pos));
				damagedBlocksQueue.erase(it);
			}

			std::vector<decltype(grenades.begin())> removedGrenades;
			for (auto it = grenades.begin(); it != grenades.end(); it++) {
				Grenade& g = **it;
				if (g.Update(dt))
					removedGrenades.push_back(it);
			}
			for (auto it : removedGrenades)
				grenades.erase(it);

			time += dt;
		}

		void World::SetMap(Handle<GameMap> newMap) {
			if (map == newMap)
				return;

			hitTestDebugger.reset();

			if (map)
				mapWrapper.reset();

			map = newMap;
			if (map) {
				mapWrapper = stmp::make_unique<GameMapWrapper>(*map);
				mapWrapper->Rebuild();
			}
		}

		void World::AddGrenade(std::unique_ptr<Grenade> g) {
			SPADES_MARK_FUNCTION_DEBUG();

			grenades.push_back(std::move(g));
		}

		void World::SetPlayer(int i, std::unique_ptr<Player> p) {
			SPADES_MARK_FUNCTION();

			players.at(i) = std::move(p);
			if (listener)
				listener->PlayerObjectSet(i);
		}

		void World::SetMode(std::unique_ptr<IGameMode> m) { mode = std::move(m); }

		void World::MarkBlockForRegeneration(const IntVector3& blockLocation) {
			UnmarkBlockForRegeneration(blockLocation);

			// Regenerate after 10 seconds
			auto result = damagedBlocksQueue.emplace(time + 10.0F, blockLocation);
			damagedBlocksQueueMap.emplace(blockLocation, result);
		}

		void World::UnmarkBlockForRegeneration(const IntVector3& blockLocation) {
			auto it = damagedBlocksQueueMap.find(blockLocation);
			if (it == damagedBlocksQueueMap.end())
				return;

			damagedBlocksQueue.erase(it->second);
			damagedBlocksQueueMap.erase(it);
		}

		static std::vector<std::vector<CellPos>>
		ClusterizeBlocks(const std::vector<CellPos>& blocks) {
			std::unordered_map<CellPos, bool, CellPosHash> blockMap;
			for (const auto& block : blocks)
				blockMap[block] = true;

			std::vector<std::vector<CellPos>> ret;
			std::deque<decltype(blockMap)::iterator> queue;

			ret.reserve(64);
			// wish I could `reserve()` queue...

			std::size_t addedCount = 0;

			for (auto it = blockMap.begin(); it != blockMap.end(); it++) {
				SPAssert(queue.empty());

				if (!it->second)
					continue;
				queue.emplace_back(it);

				std::vector<CellPos> outBlocks;

				while (!queue.empty()) {
					auto blockitem = queue.front();
					queue.pop_front();

					if (!blockitem->second)
						continue;

					auto pos = blockitem->first;
					outBlocks.emplace_back(pos);
					blockitem->second = false;

					decltype(blockMap)::iterator nextIt;

					nextIt = blockMap.find(CellPos(pos.x - 1, pos.y, pos.z));
					if (nextIt != blockMap.end() && nextIt->second)
						queue.emplace_back(nextIt);
					nextIt = blockMap.find(CellPos(pos.x + 1, pos.y, pos.z));
					if (nextIt != blockMap.end() && nextIt->second)
						queue.emplace_back(nextIt);
					nextIt = blockMap.find(CellPos(pos.x, pos.y - 1, pos.z));
					if (nextIt != blockMap.end() && nextIt->second)
						queue.emplace_back(nextIt);
					nextIt = blockMap.find(CellPos(pos.x, pos.y + 1, pos.z));
					if (nextIt != blockMap.end() && nextIt->second)
						queue.emplace_back(nextIt);
					nextIt = blockMap.find(CellPos(pos.x, pos.y, pos.z - 1));
					if (nextIt != blockMap.end() && nextIt->second)
						queue.emplace_back(nextIt);
					nextIt = blockMap.find(CellPos(pos.x, pos.y, pos.z + 1));
					if (nextIt != blockMap.end() && nextIt->second)
						queue.emplace_back(nextIt);
				}

				SPAssert(!outBlocks.empty());
				addedCount += outBlocks.size();
				ret.emplace_back(std::move(outBlocks));
			}

			SPAssert(addedCount == blocks.size());
			(void)addedCount; // used only in debug builds

			return ret;
		}

		void World::ApplyBlockActions() {
			for (const auto& creation : createdBlocks) {
				const auto& pos = creation.first;
				const auto& col = creation.second;
				uint32_t color = IntVectorToColor(col) | (100UL << 24);
				color = map->GetColorJit(color); // jit the colour
				if (map->IsSolid(pos.x, pos.y, pos.z)) {
					map->Set(pos.x, pos.y, pos.z, true, color);
					continue;
				}
				mapWrapper->AddBlock(pos.x, pos.y, pos.z, color);
			}

			std::vector<CellPos> cells;
			for (const auto& cell : destroyedBlocks) {
				if (!map->IsSolid(cell.x, cell.y, cell.z))
					continue;
				cells.emplace_back(cell);
			}
			cells = mapWrapper->RemoveBlocks(cells);

			std::vector<IntVector3> cells2;
			for (const auto& cluster : ClusterizeBlocks(cells)) {
				cells2.resize(cluster.size());
				for (std::size_t i = 0; i < cluster.size(); i++) {
					auto p = cluster[i];
					cells2[i] = IntVector3(p.x, p.y, p.z);
					map->Set(p.x, p.y, p.z, false, 0);
				}
				if (listener)
					listener->BlocksFell(cells2);
			}

			createdBlocks.clear();
			destroyedBlocks.clear();
		}

		void World::CreateBlock(spades::IntVector3 pos, spades::IntVector3 color) {
			CellPos cellp(pos.x, pos.y, pos.z);
			auto it = destroyedBlocks.find(cellp);
			if (it != destroyedBlocks.end())
				destroyedBlocks.erase(it);
			createdBlocks[cellp] = color;
		}

		void World::DestroyBlock(std::vector<spades::IntVector3>& pos) {
			bool allowToDestroy = (pos.size() == 1);
			for (const auto& p : pos) {
				if (!map->IsValidMapCoord(p.x, p.y, p.z)
					|| p.z >= (allowToDestroy ? 63 : 62))
					continue;

				CellPos cellp(p.x, p.y, p.z);
				auto it = createdBlocks.find(cellp);
				if (it != createdBlocks.end())
					createdBlocks.erase(it);
				destroyedBlocks.insert(cellp);
			}
		}

		World::PlayerPersistent& World::GetPlayerPersistent(int index) {
			SPAssert(index >= 0);
			SPAssert(index < players.size());
			return playerPersistents.at(index);
		}

		int World::CubeLineCount(IntVector3 v1, IntVector3 v2) {
			return (v2 - v1).GetManhattanLength() + 1;
		}

		std::vector<IntVector3> World::CubeLine(spades::IntVector3 v1,
			spades::IntVector3 v2, int maxLength) {
			SPADES_MARK_FUNCTION_DEBUG();

			IntVector3 c = v1;
			IntVector3 d = v2 - v1;
			long ixi, iyi, izi, dx, dy, dz, dxi, dyi, dzi;
			std::vector<IntVector3> ret;

			int VSID = map->Width();
			SPAssert(VSID == map->Height());

			int MAXZDIM = map->Depth();

			ixi = (d.x < 0) ? -1 : 1;
			iyi = (d.y < 0) ? -1 : 1;
			izi = (d.z < 0) ? -1 : 1;

			d.x = abs(d.x);
			d.y = abs(d.y);
			d.z = abs(d.z);

			int f = 0x3FFFFFFF / VSID;
			if ((d.x >= d.y) && (d.x >= d.z)) {
				dxi = 1024;
				dx = 512;
				dyi = (d.y != 0) ? (d.x * 1024 / d.y) : f;
				dy = dyi / 2;
				dzi = (d.z != 0) ? (d.x * 1024 / d.z) : f;
				dz = dzi / 2;
			} else if (d.y >= d.z) {
				dyi = 1024;
				dy = 512;
				dxi = (d.x != 0) ? (d.y * 1024 / d.x) : f;
				dx = dxi / 2;
				dzi = (d.z != 0) ? (d.y * 1024 / d.z) : f;
				dz = dzi / 2;
			} else {
				dzi = 1024;
				dz = 512;
				dxi = (d.x != 0) ? (d.z * 1024 / d.x) : f;
				dx = dxi / 2;
				dyi = (d.y != 0) ? (d.z * 1024 / d.y) : f;
				dy = dyi / 2;
			}

			if (ixi >= 0)
				dx = dxi - dx;
			if (iyi >= 0)
				dy = dyi - dy;
			if (izi >= 0)
				dz = dzi - dz;

			while (1) {
				ret.push_back(c);

				if (ret.size() == (size_t)maxLength)
					break;
				if (c == v2)
					break; // we have reached the end block

				if ((dz <= dx) && (dz <= dy)) {
					c.z += izi;
					if (c.z < 0 || c.z >= MAXZDIM)
						break; // we have reached the z bounds of the map
					dz += dzi;
				} else if (dx < dy) {
					c.x += ixi;
					if (c.x < 0 || c.x >= VSID)
						break;
					dx += dxi;
				} else {
					c.y += iyi;
					if (c.y < 0 || c.y >= VSID)
						break;
					dy += dyi;
				}
			}

			return ret;
		}

		World::WeaponRayCastResult World::WeaponRayCast(spades::Vector3 startPos,
			spades::Vector3 dir, stmp::optional<int> excludePlayerId) {
			WeaponRayCastResult result;
			bool hasHitPlayer = false;
			int hitPlayerId = 0;
			float hitPlayerDist = 0.0F;
			hitTag_t hitFlag = hit_None;

			bool interp = cg_orientationSmoothing;

			for (int i = 0; i < (int)players.size(); i++) {
				const auto& p = players[i];
				if (!p || (excludePlayerId && *excludePlayerId == i))
					continue;

				if (!p->IsAlive() || p->IsSpectator())
					continue; // filter deads/spectators
				if (!p->RayCastApprox(startPos, dir))
					continue; // quickly reject players unlikely to be hit

				Vector3 hitPos;
				Player::HitBoxes hb = p->GetHitBoxes(interp); // interpolated
				if (hb.head.RayCast(startPos, dir, &hitPos)) {
					float const dist = (hitPos - startPos).GetSquaredLength();
					if (!hasHitPlayer || dist < hitPlayerDist) {
						if (!hasHitPlayer || hitPlayerId != i) {
							hitPlayerId = i;
							hasHitPlayer = true;
							hitFlag = hit_None;
						}

						hitPlayerDist = dist;
						hitFlag |= hit_Head;
					}
				}

				if (hb.torso.RayCast(startPos, dir, &hitPos)) {
					float const dist = (hitPos - startPos).GetSquaredLength();
					if (!hasHitPlayer || dist < hitPlayerDist) {
						if (!hasHitPlayer || hitPlayerId != i) {
							hitPlayerId = i;
							hasHitPlayer = true;
							hitFlag = hit_None;
						}

						hitPlayerDist = dist;
						hitFlag |= hit_Torso;
					}
				}

				for (int j = 0; j < 3; j++) {
					if (hb.limbs[j].RayCast(startPos, dir, &hitPos)) {
						float const dist = (hitPos - startPos).GetSquaredLength();
						if (!hasHitPlayer || dist < hitPlayerDist) {
							if (!hasHitPlayer || hitPlayerId != i) {
								hitPlayerId = i;
								hasHitPlayer = true;
								hitFlag = hit_None;
							}

							hitPlayerDist = dist;
							hitFlag |= (j == 2) ? hit_Arms : hit_Legs;
						}
					}
				}
			}

			// do map raycast
			GameMap::RayCastResult mapResult;
			mapResult = map->CastRay2(startPos, dir, 256);

			float hitBlockDist = (mapResult.hitPos - startPos).GetSquaredLength();

			if (mapResult.hit && hitBlockDist <= FOG_DISTANCE_SQ &&
				(!hasHitPlayer || hitBlockDist < hitPlayerDist)) {
				result.hit = true;
				result.startSolid = mapResult.startSolid;
				result.blockPos = mapResult.hitBlock;
				result.hitPos = mapResult.hitPos;
				result.hitFlag = hit_None;
			} else if (hasHitPlayer) {
				result.hit = true;
				result.startSolid = false; // FIXME: startSolid for player
				result.playerId = hitPlayerId;
				result.hitPos = startPos + dir * sqrtf(hitPlayerDist);
				result.hitFlag = hitFlag;
			} else {
				result.hit = false;
			}

			return result;
		}

		HitTestDebugger* World::GetHitTestDebugger() {
			if (cg_debugHitTest) {
				if (hitTestDebugger == nullptr)
					hitTestDebugger = stmp::make_unique<HitTestDebugger>(this);
				return hitTestDebugger.get();
			}
			return nullptr;
		}

		bool World::IsModeFFA() {
			if (gameProperties->isGameModeFFA)
				return true; // if this fails we check for color matching
			return teams[0].color == teams[1].color;
		}
	} // namespace client
} // namespace spades
