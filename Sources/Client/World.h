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

#pragma once

#include <array>
#include <list>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "GameConstants.h"
#include "GameMapWrapper.h"
#include <Core/Debug.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>
#include <Core/TMPUtils.h>

namespace spades {
	namespace client {
		class GameMap;
		class GameMapWrapper;
		class Player;
		class IWorldListener;
		class Grenade;
		class IGameMode;
		class HitTestDebugger;
		struct GameProperties;

		constexpr std::size_t NumPlayerSlots = 256;

		class World {
		public:
			struct Team {
				IntVector3 color;
				std::string name;
			};
			struct PlayerPersistent {
				std::string name;
				int score;
				PlayerPersistent() : score(0) { ; }
			};

		private:
			IWorldListener* listener = nullptr;

			std::unique_ptr<IGameMode> mode;

			Handle<GameMap> map;
			std::unique_ptr<GameMapWrapper> mapWrapper;
			float time = 0.0F;
			IntVector3 fogColor;
			Team teams[3];

			std::shared_ptr<GameProperties> gameProperties;

			std::array<std::unique_ptr<Player>, NumPlayerSlots> players;
			std::array<PlayerPersistent, NumPlayerSlots> playerPersistents;
			stmp::optional<int> localPlayerIndex;

			std::list<std::unique_ptr<Grenade>> grenades;
			std::unique_ptr<HitTestDebugger> hitTestDebugger;

			std::unordered_map<CellPos, spades::IntVector3, CellPosHash> createdBlocks;
			std::unordered_set<CellPos, CellPosHash> destroyedBlocks;

			std::multimap<float, IntVector3> damagedBlocksQueue;
			std::unordered_map<IntVector3, std::multimap<float, IntVector3>::iterator>
			  damagedBlocksQueueMap;

			void ApplyBlockActions();

		public:
			World(const std::shared_ptr<GameProperties>&);
			~World();
			const Handle<GameMap>& GetMap() { return map; }
			GameMapWrapper& GetMapWrapper() { return *mapWrapper; }
			float GetTime() { return time; }
			int GetTimeMS() { return (int)(time * 1000); }

			/** Returns a non-null reference to `GameProperties`. */
			const std::shared_ptr<GameProperties>& GetGameProperties() { return gameProperties; }

			void SetMap(Handle<GameMap>);

			IntVector3 GetFogColor() { return fogColor; }
			void SetFogColor(IntVector3 v) { fogColor = v; }

			/** Steps player physics and game logic. Must be driven at the fixed
			 * rate the server simulates at, or the client will diverge from it. */
			void UpdatePlayers(float dt);
			/** Advances view-only orientation smoothing for remote players. Safe to
			 * drive at any rate; it affects presentation, not simulation. */
			void UpdatePlayersSmooth(float dt);
			void Advance(float dt);

			void AddGrenade(std::unique_ptr<Grenade>);
			const std::list<std::unique_ptr<Grenade>>& GetAllGrenades() { return grenades; }

			void MarkBlockForRegeneration(const IntVector3& blockLocation);
			void UnmarkBlockForRegeneration(const IntVector3& blockLocation);

			int CubeLineCount(IntVector3 v1, IntVector3 v2);
			std::vector<IntVector3> CubeLine(IntVector3 v1, IntVector3 v2, int maxLength);

			stmp::optional<Player&> GetPlayer(unsigned int i) {
				SPAssert(i < players.size());
				return players[i].get();
			}

			void SetPlayer(int i, std::unique_ptr<Player> p);

			/**
			 * Get the object containing data specific to the current game mode.
			 * Can be `{}` if the game mode is not specified yet.
			 */
			stmp::optional<IGameMode&> GetMode() { return mode.get(); }
			void SetMode(std::unique_ptr<IGameMode>);

			Team& GetTeam(int t) {
				if (t >= 2 || t < 0) // spectator
					return teams[2];
				return teams[t];
			}
			std::string GetTeamName(int t) { return GetTeam(t).name; }
			IntVector3 GetTeamColor(int t) { return GetTeam(t).color; }

			bool IsModeFFA();

			PlayerPersistent& GetPlayerPersistent(int index);
			std::string GetPlayerName(int index) { return GetPlayerPersistent(index).name; }
			int GetPlayerScore(int index) { return GetPlayerPersistent(index).score; }

			void CreateBlock(IntVector3 pos, IntVector3 color);
			void DestroyBlock(std::vector<IntVector3>& pos);

			struct WeaponRayCastResult {
				bool hit, startSolid;
				stmp::optional<int> playerId;
				IntVector3 blockPos;
				Vector3 hitPos;
				hitTag_t hitFlag;
			};

			WeaponRayCastResult WeaponRayCast(Vector3 startPos, Vector3 dir,
											  stmp::optional<int> excludePlayerId);

			size_t GetNumPlayerSlots() { return players.size(); }
			size_t GetNumPlayers();

			stmp::optional<int> GetLocalPlayerIndex() { return localPlayerIndex; }
			void SetLocalPlayerIndex(stmp::optional<int> p) { localPlayerIndex = p; }

			/** Get the local player. Can be `nullptr`. */
			stmp::optional<Player&> GetLocalPlayer() {
				if (!GetLocalPlayerIndex())
					return {};
				return GetPlayer(*GetLocalPlayerIndex());
			}

			/** Can be `nullptr`. */
			HitTestDebugger* GetHitTestDebugger();

			void SetListener(IWorldListener* newListener) { listener = newListener; }
			IWorldListener* GetListener() { return listener; }
		};
	} // namespace client
} // namespace spades
