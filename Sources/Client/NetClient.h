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

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "DemoRecorder.h"
#include "GameConstants.h"
#include "INetClient.h"
#include "NetProtocol.h"
#include "Player.h"
#include <Core/Debug.h>
#include <Core/Math.h>
#include <Core/ServerAddress.h>
#include <Core/Stopwatch.h>
#include <Core/VersionInfo.h>
#include <ZeroSpades.h>

struct _ENetHost;
struct _ENetPeer;
typedef _ENetHost ENetHost;
typedef _ENetPeer ENetPeer;

namespace spades {
	namespace client {
		class Client;
		class Player;

		enum NetExtensionType {
			ExtensionTypePlayerProperties = 0,
			ExtensionTypeExtendedTeamplay = 2,
			ExtensionTypePlayerLimit = 192,
			ExtensionTypeMessageTypes = 193,
			ExtensionTypeKickReason = 194,
		};

		class World;
		class NetPacketWriter;
		struct PlayerInput;
		struct WeaponInput;
		class Grenade;
		struct GameProperties;
		class GameMapLoader;

		class NetClient : public INetClient {
			Client* client;
			NetClientStatus status;
			ENetHost* host;
			ENetPeer* peer;
			std::string statusString;

			class MapDownloadMonitor {
				Stopwatch sw;
				unsigned int numBytesDownloaded;
				GameMapLoader& mapLoader;
				bool receivedFirstByte;

			public:
				MapDownloadMonitor(GameMapLoader&);

				void AccumulateBytes(unsigned int);
				std::string GetDisplayedText();
			};

			/** Only valid in the `NetClientStatusReceivingMap` state */
			std::unique_ptr<GameMapLoader> mapLoader;
			/** Only valid in the `NetClientStatusReceivingMap` state */
			std::unique_ptr<MapDownloadMonitor> mapLoadMonitor;

			std::shared_ptr<GameProperties> properties;

			int protocolVersion;
			/** Extensions supported by both client and server (map of extension id → version) */
			std::unordered_map<uint8_t, uint8_t> extensions;
			/** Extensions implemented in this client (map of extension id → version) */
			std::unordered_map<uint8_t, uint8_t> implementedExtensions{
			  {ExtensionTypePlayerProperties, 1},
			  {ExtensionTypeExtendedTeamplay, 1},
			  {ExtensionTypePlayerLimit, 1},
			  {ExtensionTypeMessageTypes, 1},
			  {ExtensionTypeKickReason, 1}};

			class BandwidthMonitor {
				ENetHost* host;
				Stopwatch sw;
				double lastDown;
				double lastUp;

			public:
				BandwidthMonitor(ENetHost*);
				double GetDownlinkBps() { return lastDown * 8.0; }
				double GetUplinkBps() { return lastUp * 8.0; }
				void Update();
			};

			std::unique_ptr<BandwidthMonitor> bandwidthMonitor;

			std::unique_ptr<DemoRecorder> demoRecorder;

			std::vector<Vector3> savedPlayerPos;
			std::vector<Vector3> savedPlayerFront;
			std::vector<int> savedPlayerTeam;

			std::vector<std::vector<char>> savedPackets;

			unsigned int lastPlayerInput;
			unsigned int lastWeaponInput;

			// used for some scripts including Arena
			IntVector3 temporaryPlayerBlockColor;

			bool HandleHandshakePackets(NetPacketReader&);
			void HandleExtensionPacket(NetPacketReader&);
			void HandleExtendedTeamplayPacket(NetPacketReader&);

			/** Whether the server negotiated the given extension during the handshake. */
			bool HasExtension(NetExtensionType type) const {
				return extensions.find(static_cast<uint8_t>(type)) != extensions.end();
			}

			void HandleGamePacket(NetPacketReader&);
			stmp::optional<World&> GetWorld();
			Player& GetPlayer(int);
			stmp::optional<Player&> GetPlayerOrNull(int);
			Player& GetLocalPlayer();
			stmp::optional<Player&> GetLocalPlayerOrNull();

			std::string customKickReasonString;
			std::string DisconnectReasonString(uint32_t);
			void CaptureKickReason(NetPacketReader&);

			void MapLoaded();

			/** Writes the initial game state to the demo recorder (map, players, etc.) */
			void WriteInitialDemoState();

			void SendMapCached();
			void SendVersion();
			void SendVersionEnhanced(const std::set<std::uint8_t>& propertyIds);
			void SendSupportedExtensions();

		public:
			NetClient(Client*);
			~NetClient() override;

			NetClientStatus GetStatus() override { return status; }
			std::string GetStatusString() override;

			/**
			 * Gets how much portion of the map has completed loading.
			 * `GetStatus()` must be `NetClientStatusReceivingMap`.
			 *
			 * @return A value in range `[0, 1]`.
			 */
			float GetMapReceivingProgress() override;

			/**
			 * Return a non-null reference to `GameProperties` for this connection.
			 * Must be the connected state.
			 */
			const std::shared_ptr<GameProperties>& GetGameProperties() override {
				SPAssert(properties);
				return properties;
			}

			void Connect(const ServerAddress& hostname);
			void Disconnect() override;

			int GetPing() override;
			float GetPacketLoss() override;
			float GetPacketThrottle() override;

			// INetClient::DoEvents — picks poll timeout based on connection status.
			void DoEvents(float dt) override;
			// Direct timeout control for internal/test use.
			void DoEvents(int timeout);

			void SendJoin(int team, WeaponType, std::string name, int score) override;
			void SendPosition(Vector3) override;
			void SendOrientation(Vector3) override;
			void SendPlayerInput(PlayerInput) override;
			void SendWeaponInput(WeaponInput) override;
			void SendHit(int targetPlayerId, HitType type) override;
			void SendGrenade(const Grenade&) override;
			void SendTool() override;
			void SendHeldBlockColor() override;
			void SendBlockAction(IntVector3, BlockActionType) override;
			void SendBlockLine(IntVector3 v1, IntVector3 v2) override;
			void SendChat(std::string, bool global) override;
			void SendReload() override;
			void SendTeamChange(int team) override;
			void SendWeaponChange(WeaponType) override;
			void SendTeamplayPing(Vector3 position, const std::string& reason) override;
			void SendHandShakeValid(int challenge);

			double GetDownlinkBps() override { return bandwidthMonitor->GetDownlinkBps(); }
			double GetUplinkBps() override { return bandwidthMonitor->GetUplinkBps(); }

			// Demo recording
			bool StartDemoRecording(const std::string& filename = "",
			                        const std::string& context = "");
			void StopDemoRecording();
			bool IsDemoRecording() const;
			float GetDemoRecordingTime() const;
			uint64_t GetDemoPacketCount() const;
			const std::string& GetDemoFilename() const;
		};
	} // namespace client
} // namespace spades