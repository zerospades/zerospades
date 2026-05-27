/*
 Copyright (c) 2026 Francois ND
 based on code of OpenSpades (c) yvt 2013.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "DemoPlayer.h"
#include "GameConstants.h"
#include "GameMap.h"
#include "INetClient.h"
#include "Player.h"
#include <Core/Math.h>

namespace spades {
	namespace client {
		class Client;
		class GameMapLoader;
		struct GameProperties;

		/**
		 * DemoNetClient provides a NetClient-like interface that reads packets from
		 * a demo file instead of from the network. This allows the Client class to
		 * play back recorded demos with minimal changes.
		 */
		class DemoNetClient : public INetClient {
			Client* client;
			NetClientStatus status;
			std::unique_ptr<DemoPlayer> demoPlayer;
			std::unique_ptr<GameMapLoader> mapLoader;
			std::shared_ptr<GameProperties> properties;
			std::string statusString;

			// Packet handling state (mirrors NetClient)
			std::vector<std::vector<char>> preMapPackets;
			std::vector<Vector3> savedPlayerPos;
			std::vector<Vector3> savedPlayerFront;
			std::vector<int> savedPlayerTeam;
			IntVector3 temporaryPlayerBlockColor;

			// Debug tracking for map loading
			size_t expectedMapSize;
			size_t receivedMapBytes;

			// The player ID that was recorded as local player (for follow-cam)
			int recordedLocalPlayerId;

			// Snapshot of the map at t=0 (after MapLoaded), used to restore state on backward seek
			Handle<GameMap> initialMap;

			// True while fast-replaying packets after a backward seek; suppresses client callbacks
			bool seekingMode;

			stmp::optional<World&> GetWorld();
			Player& GetPlayer(int);
			stmp::optional<Player&> GetPlayerOrNull(int);
			Player& GetLocalPlayer();
			stmp::optional<Player&> GetLocalPlayerOrNull();

			void ProcessPacket(const std::vector<char>& data);
			void HandleGamePacket(class NetPacketReader& reader);
			void MapLoaded();

			// Reset the world to initial map state and reset tracking variables
			void ResetWorldForReplay();
			// Replay all demo packets from index 0 up to targetTime with seekingMode=true
			void FastReplay(float targetTime);

		public:
			DemoNetClient(Client* client);
			~DemoNetClient() override;

			/**
			 * Opens a demo file for playback.
			 * @param filename Path to the demo file
			 * @return true if successful
			 */
			bool OpenDemo(const std::string& filename);

			// INetClient interface
			void DoEvents(float dt) override;
			NetClientStatus GetStatus() override { return status; }
			std::string GetStatusString() override;
			float GetMapReceivingProgress() override;
			const std::shared_ptr<GameProperties>& GetGameProperties() override { return properties; }

			// Playback controls (DemoNetClient-specific, not in INetClient)
			void Pause() { if (demoPlayer) demoPlayer->Pause(); }
			void Resume() { if (demoPlayer) demoPlayer->Resume(); }
			void TogglePause() { if (demoPlayer) demoPlayer->TogglePause(); }
			void SetSpeed(float speed) { if (demoPlayer) demoPlayer->SetSpeed(speed); }
			void Seek(float time);
			/**
			 * Updates the displayed playback position without replaying world state.
			 * Use during interactive scrubbing to keep the HUD responsive; call Seek()
			 * once scrubbing ends to commit the world to the new position.
			 */
			void SeekPreview(float time);
			void SeekToBeginning();
			float GetTime() const { return demoPlayer ? demoPlayer->GetTime() : 0.0f; }
			float GetDuration() const { return demoPlayer ? demoPlayer->GetDuration() : 0.0f; }
			bool IsFinished() const { return demoPlayer ? demoPlayer->IsFinished() : true; }
			bool IsPaused() const { return demoPlayer ? demoPlayer->IsPaused() : false; }
			float GetSpeed() const { return demoPlayer ? demoPlayer->GetSpeed() : 1.0f; }
			int GetRecordedLocalPlayerId() const { return recordedLocalPlayerId; }

			// Stub network methods (no-ops — demo playback sends nothing to a server)
			void Disconnect() override { status = NetClientStatusNotConnected; }
			int GetPing() override { return 0; }
			float GetPacketLoss() override { return 0.0f; }
			float GetPacketThrottle() override { return 1.0f; }
			double GetDownlinkBps() override { return 0.0; }
			double GetUplinkBps() override { return 0.0; }

			void SendJoin(int, WeaponType, std::string, int) override {}
			void SendPosition(Vector3) override {}
			void SendOrientation(Vector3) override {}
			void SendPlayerInput(PlayerInput) override {}
			void SendWeaponInput(WeaponInput) override {}
			void SendHit(int, HitType) override {}
			void SendGrenade(const Grenade&) override {}
			void SendTool() override {}
			void SendHeldBlockColor() override {}
			void SendBlockAction(IntVector3, BlockActionType) override {}
			void SendBlockLine(IntVector3, IntVector3) override {}
			void SendChat(std::string, bool) override {}
			void SendReload() override {}
			void SendTeamChange(int) override {}
			void SendWeaponChange(WeaponType) override {}

		};
	} // namespace client
} // namespace spades
