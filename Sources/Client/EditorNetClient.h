/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades.

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

#include <string>

#include "INetClient.h"
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class GameMap;
		class Client;
		struct GameProperties;

		/**
		 * EditorNetClient provides a local game environment for the map editor.
		 *
		 * It functions like DemoNetClient but instead of playing back recorded packets,
		 * it creates a minimal local game state with a loaded map and a local player
		 * in spectator/edit mode.
		 */
		class EditorNetClient : public INetClient {
			Client* client;
			NetClientStatus status;
			Handle<GameMap> map;
			std::shared_ptr<GameProperties> properties;
			std::string statusString;
			int localPlayerId;

		public:
			EditorNetClient(Client* client);
			~EditorNetClient() override;

			// Load a map from the specified file path
			bool LoadMap(const std::string& mapPath);

			// Update spectator camera position (called by Client each frame)
			void SetSpectatorPosition(const Vector3& pos) {
				lastSpectatorPos = pos;
				spectatorPosSet = true;
			}

			// Update player position and handle respawn on death
			void UpdatePlayerState();

			// INetClient interface
			NetClientStatus GetStatus() override { return status; }
			std::string GetStatusString() override { return statusString; }
			float GetMapReceivingProgress() override { return 1.0f; }
			const std::shared_ptr<GameProperties>& GetGameProperties() override { return properties; }
			void DoEvents(float dt) override;
			int GetPing() override { return 0; }
			float GetPacketLoss() override { return 0.0f; }
			float GetPacketThrottle() override { return 0.0f; }
			double GetDownlinkBps() override { return 0.0; }
			double GetUplinkBps() override { return 0.0; }
			void Disconnect() override;
			void SendJoin(int team, WeaponType, std::string name, int score) override;
			void SendPosition(Vector3) override {}
			void SendOrientation(Vector3) override {}
			void SendPlayerInput(PlayerInput) override {}
			void SendWeaponInput(WeaponInput) override {}
			void SendHit(int targetPlayerId, HitType) override;
			void SendGrenade(const Grenade&) override;
			void SendTool() override {}
			void SendHeldBlockColor() override {}
			void SendBlockAction(IntVector3, BlockActionType) override;
			void SendBlockLine(IntVector3 v1, IntVector3 v2) override;
			void SendChat(std::string, bool global) override {}
			void SendReload() override {}
			void SendTeamChange(int team) override;
			void SendWeaponChange(WeaponType) override {}

		private:
			Vector3 lastSpectatorPos;
			Vector3 lastPlayerPos;
			bool spectatorPosSet = false;
			void InitializeLocalPlayer();
			void HandleTeamSwitch(int teamId);
			friend class Client;
		};
	} // namespace client
} // namespace spades
