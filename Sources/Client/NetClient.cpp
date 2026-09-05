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

#include <math.h>
#include <string.h>
#include <vector>

#include <enet/enet.h>

#include "CTFGameMode.h"
#include "Client.h"
#include "ExtendedTeamplay.h"
#include "GameMap.h"
#include "NetProtocol.h"
#include "GameMapLoader.h"
#include "GameProperties.h"
#include "Grenade.h"
#include "NetClient.h"
#include "Player.h"
#include "TCGameMode.h"
#include "Weapon.h"
#include "World.h"
#include <Core/CP437.h>
#include <Core/Debug.h>
#include <Core/DeflateStream.h>
#include <Core/Exception.h>
#include <Core/Math.h>
#include <Core/DynamicMemoryStream.h>
#include <Core/MemoryStream.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Core/TMPUtils.h>

DEFINE_SPADES_SETTING(cg_unicode, "1");

DEFINE_SPADES_SETTING(cg_defaultBlockColorR, "111");
DEFINE_SPADES_SETTING(cg_defaultBlockColorG, "111");
DEFINE_SPADES_SETTING(cg_defaultBlockColorB, "111");

namespace spades {
	namespace client {

		namespace {
			const char UTFSign = -1;

			enum class VersionInfoPropertyId : std::uint8_t {
				ApplicationNameAndVersion = 0,
				UserLocale = 1,
				ClientFeatureFlags1 = 2
			};

			enum class ClientFeatureFlags1 : std::uint32_t { None = 0, SupportsUnicode = 1 << 0 };

			ClientFeatureFlags1 operator|(ClientFeatureFlags1 a, ClientFeatureFlags1 b) {
				return (ClientFeatureFlags1)((uint32_t)a | (uint32_t)b);
			}
			ClientFeatureFlags1& operator|=(ClientFeatureFlags1& a, ClientFeatureFlags1 b) {
				return a = a | b;
			}

			std::string EncodeString(std::string str) {
				auto str2 = CP437::Encode(str, -1);
				if (!cg_unicode)
					return str2; // ignore fallbacks

				// some fallbacks; always use UTF8
				if (str2.find(-1) != std::string::npos)
					str.insert(0, &UTFSign, 1);
				else
					str = str2;

				return str;
			}

			static WeaponType GetWeaponType(int weapon) {
				switch (weapon) {
					case 0: return RIFLE_WEAPON;
					case 1: return SMG_WEAPON;
					case 2: return SHOTGUN_WEAPON;
					default: SPRaise("Received invalid weapon: %d", weapon);
				}
			}
			static int GetWeaponId(WeaponType type) {
				switch (type) {
					case RIFLE_WEAPON: return 0;
					case SMG_WEAPON: return 1;
					case SHOTGUN_WEAPON: return 2;
					default: SPInvalidEnum("weapon", type);
				}
			}
			static Player::ToolType GetToolType(int tool) {
				switch (tool) {
					case 0: return Player::ToolSpade;
					case 1: return Player::ToolBlock;
					case 2: return Player::ToolWeapon;
					case 3: return Player::ToolGrenade;
					default: SPRaise("Received invalid tool: %d", tool);
				}
			}
			static int GetToolId(Player::ToolType type) {
				switch (type) {
					case Player::ToolSpade: return 0;
					case Player::ToolBlock: return 1;
					case Player::ToolWeapon: return 2;
					case Player::ToolGrenade: return 3;
					default: SPInvalidEnum("tool", type);
				}
			}
		} // namespace

		class NetPacketWriter {
			std::vector<char> data;

		public:
			NetPacketWriter(PacketType type) { data.push_back(type); }

			void WriteByte(uint8_t v) {
				SPADES_MARK_FUNCTION_DEBUG();
				data.push_back(v);
			}
			void WriteShort(uint16_t v) {
				SPADES_MARK_FUNCTION_DEBUG();
				data.push_back((char)(v));
				data.push_back((char)(v >> 8));
			}
			void WriteInt(uint32_t v) {
				SPADES_MARK_FUNCTION_DEBUG();
				data.push_back((char)(v));
				data.push_back((char)(v >> 8));
				data.push_back((char)(v >> 16));
				data.push_back((char)(v >> 24));
			}
			void WriteFloat(float v) {
				SPADES_MARK_FUNCTION_DEBUG();
				union {
					float f;
					uint32_t i;
				};
				f = v;
				WriteInt(i);
			}

			void WriteColor(IntVector3 v) {
				WriteByte((uint8_t)v.z); // B
				WriteByte((uint8_t)v.y); // G
				WriteByte((uint8_t)v.x); // R
			}
			void WriteIntVector3(IntVector3 v) {
				WriteInt((uint32_t)v.x);
				WriteInt((uint32_t)v.y);
				WriteInt((uint32_t)v.z);
			}
			void WriteVector3(const Vector3& v) {
				WriteFloat(v.x);
				WriteFloat(v.y);
				WriteFloat(v.z);
			}

			void WriteString(std::string str) {
				str = EncodeString(str);
				data.insert(data.end(), str.begin(), str.end());
			}

			/** Writes the bytes as they are, for a field the protocol defines as UTF-8
			 * text rather than as the base protocol's Code Page 437 with a UTF-8 escape. */
			void WriteRawString(const std::string& str) {
				data.insert(data.end(), str.begin(), str.end());
			}

			void WriteString(const std::string& str, size_t fillLen) {
				WriteString(str.substr(0, fillLen));
				size_t sz = str.size();
				while (sz < fillLen) {
					WriteByte((uint8_t)0);
					sz++;
				}
			}

			std::size_t GetPosition() { return data.size(); }

			void Update(std::size_t position, std::uint8_t newValue) {
				SPADES_MARK_FUNCTION_DEBUG();

				if (position >= data.size()) {
					SPRaise("Invalid write (%d should be less than %d)",
						(int)position, (int)data.size());
				}

				data[position] = static_cast<char>(newValue);
			}

			void Update(std::size_t position, std::uint32_t newValue) {
				SPADES_MARK_FUNCTION_DEBUG();

				if (position + 4 > data.size()) {
					SPRaise("Invalid write (%d should be less than or equal to %d)",
							(int)(position + 4), (int)data.size());
				}

				// Assuming the target platform is little endian and supports
				// unaligned memory access...
				*reinterpret_cast<std::uint32_t*>(data.data() + position) = newValue;
			}

			ENetPacket* CreatePacket(int flag = ENET_PACKET_FLAG_RELIABLE) {
				return enet_packet_create(data.data(), data.size(), flag);
			}

			const std::vector<char>& GetData() const { return data; }
		};

		NetClient::NetClient(Client* c) : client(c), host(nullptr), peer(nullptr) {
			SPADES_MARK_FUNCTION();

			enet_initialize();
			SPLog("ENet initialized");

			host = enet_host_create(NULL, 1, 1, 100000, 100000);
			SPLog("ENet host created");
			if (!host)
				SPRaise("Failed to create ENet host");

			if (enet_host_compress_with_range_coder(host) < 0)
				SPRaise("Failed to enable ENet Range coder.");

			SPLog("ENet Range Coder Enabled");

			peer = NULL;
			status = NetClientStatusNotConnected;

			lastPlayerInput = 0;
			lastWeaponInput = 0;

			const int slots = 256;
			savedPlayerPos.resize(slots);
			savedPlayerFront.resize(slots);
			savedPlayerTeam.resize(slots);

			std::fill(savedPlayerTeam.begin(), savedPlayerTeam.end(), -1);

			bandwidthMonitor.reset(new BandwidthMonitor(host));
			demoRecorder.reset(new DemoRecorder());
		}
		NetClient::~NetClient() {
			SPADES_MARK_FUNCTION();

			if (demoRecorder && demoRecorder->IsRecording())
				demoRecorder->StopRecording();

			Disconnect();
			if (host)
				enet_host_destroy(host);
			bandwidthMonitor.reset();
			demoRecorder.reset();
			SPLog("ENet host destroyed");
		}

		void NetClient::Connect(const ServerAddress& hostname) {
			SPADES_MARK_FUNCTION();

			Disconnect();
			SPAssert(status == NetClientStatusNotConnected);

			switch (hostname.GetProtocolVersion()) {
				case ProtocolVersion::v075:
					SPLog("Using Ace of Spades 0.75 protocol");
					protocolVersion = 3;
					break;
				case ProtocolVersion::v076:
					SPLog("Using Ace of Spades 0.76 protocol");
					protocolVersion = 4;
					break;
				default: SPRaise("Invalid ProtocolVersion"); break;
			}

			ENetAddress addr = hostname.GetENetAddress();
			SPLog("Connecting to %u:%u", (unsigned int)addr.host, (unsigned int)addr.port);

			savedPackets.clear();
			customKickReasonString.clear();

			peer = enet_host_connect(host, &addr, 1, protocolVersion);
			if (peer == NULL)
				SPRaise("Failed to create ENet peer");

			properties.reset(new GameProperties(hostname.GetProtocolVersion()));

			status = NetClientStatusConnecting;
			statusString = _Tr("NetClient", "Connecting to the server");
		}

		void NetClient::Disconnect() {
			SPADES_MARK_FUNCTION();

			if (!peer)
				return;

			enet_peer_disconnect(peer, 0);
			status = NetClientStatusNotConnected;
			statusString = _Tr("NetClient", "Not connected");

			savedPackets.clear();

			ENetEvent event;
			SPLog("Waiting for graceful disconnection");
			while (enet_host_service(host, &event, 1000) > 0) {
				switch (event.type) {
					case ENET_EVENT_TYPE_RECEIVE:
						enet_packet_destroy(event.packet); break;
					case ENET_EVENT_TYPE_DISCONNECT:
						// disconnected safely
						// FIXME: release peer
						enet_peer_reset(peer);
						peer = NULL;
						return;
					default:;
						// discard
				}
			}

			SPLog("Connection terminated");
			enet_peer_reset(peer);
			// FXIME: release peer
			peer = NULL;
		}

		int NetClient::GetPing() {
			SPADES_MARK_FUNCTION();

			if (status == NetClientStatusNotConnected)
				return -1;

			auto rtt = peer->roundTripTime;
			if (rtt == 0)
				return -1;
			return static_cast<int>(rtt);
		}

		float NetClient::GetPacketLoss() {
			SPADES_MARK_FUNCTION();

			if (status == NetClientStatusNotConnected)
				return -1;

			return static_cast<float>(peer->packetLoss) / ENET_PEER_PACKET_LOSS_SCALE;
		}

		float NetClient::GetPacketThrottle() {
			if (status == NetClientStatusNotConnected)
				return -1;

			return static_cast<float>(peer->packetThrottle) / ENET_PEER_PACKET_THROTTLE_SCALE;
		}

		void NetClient::DoEvents(float /*dt*/) {
			DoEvents(status == NetClientStatusConnected ? 0 : 10);
		}

		void NetClient::DoEvents(int timeout) {
			SPADES_MARK_FUNCTION();

			if (status == NetClientStatusNotConnected)
				return;

			if (bandwidthMonitor)
				bandwidthMonitor->Update();

			ENetEvent event;
			while (enet_host_service(host, &event, timeout) > 0) {
				if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
					if (GetWorld())
						client->SetWorld(NULL);

					enet_peer_reset(peer);
					peer = NULL;
					status = NetClientStatusNotConnected;

					std::string reasonStr = DisconnectReasonString(event.data);
					SPLog("Disconnected (data = 0x%08x)", (unsigned int)event.data);
					statusString = "Disconnected: " + reasonStr;
					SPRaise("Disconnected: %s", reasonStr.c_str());
				}

				stmp::optional<NetPacketReader> readerOrNone;
				if (event.type == ENET_EVENT_TYPE_RECEIVE) {
					std::vector<char> packetData(event.packet->data,
					                             event.packet->data + event.packet->dataLength);
					enet_packet_destroy(event.packet);
					readerOrNone.reset(std::move(packetData));
					auto& reader = readerOrNone.value();

					// Record packet for demo if recording is active.
					// Skip the server's WeaponReload echo for the local player: the
					// client-sent packet is already recorded in SendReload(), so
					// recording the server response would produce a double reload.
					if (demoRecorder && demoRecorder->IsRecording()) {
						auto data = reader.GetData();
						bool skip = false;
						if (data.size() >= 2 &&
						    static_cast<uint8_t>(data[0]) == PacketTypeWeaponReload) {
							auto localPlayer = GetLocalPlayerOrNull();
							if (localPlayer &&
							    static_cast<uint8_t>(data[1]) == static_cast<uint8_t>(localPlayer->GetId()))
								skip = true;
						}
						if (!skip)
							demoRecorder->RecordPacket(data.data(), data.size());
					}

					try {
						if (HandleHandshakePackets(reader))
							continue;
					} catch (const std::exception& ex) {
						int type = reader.GetType();
						reader.DumpDebug();
						SPRaise("Exception while handling packet type 0x%08x:\n%s", type, ex.what());
					}
				}

				if (status == NetClientStatusConnecting) {
					if (event.type == ENET_EVENT_TYPE_CONNECT) {
						statusString = _Tr("NetClient", "Awaiting for state");
					} else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
						auto& reader = readerOrNone.value();
						int type = reader.GetType();
						reader.DumpDebug();

						if (type != PacketTypeMapStart)
							SPRaise("Unexpected packet: %d", type);

						auto mapSize = reader.ReadInt();
						SPLog("Map size advertised by the server: %lu", (unsigned long)mapSize);

						mapLoader.reset(new GameMapLoader());
						mapLoadMonitor.reset(new MapDownloadMonitor(*mapLoader));

						status = NetClientStatusReceivingMap;
						statusString = _Tr("NetClient", "Loading snapshot");
					}
				} else if (status == NetClientStatusReceivingMap) {
					SPAssert(mapLoader);

					if (event.type == ENET_EVENT_TYPE_RECEIVE) {
						auto& reader = readerOrNone.value();
						int type = reader.GetType();

						if (type == PacketTypeMapChunk) {
							std::vector<char> dt = reader.GetData();

							mapLoader->AddRawChunk(dt.data() + 1, dt.size() - 1);
							mapLoadMonitor->AccumulateBytes(
							  static_cast<unsigned int>(dt.size() - 1));
						} else {
							reader.DumpDebug();

							// The actual size of the map data cannot be known beforehand because
							// of compression. This means we must detect the end of the map
							// transfer in another way.
							//
							// We do this by checking for a StateData packet, which is sent
							// directly after the map transfer completes.
							//
							// A number of other packets can also be received while loading the map:
							//
							//	- World update packets (WorldUpdate, ExistingPlayer, and
							//	  CreatePlayer) for the current round. We must store such packets
							//	  temporarily and process them later when a `World` is created.
							//
							//	- Leftover reload packet from the previous round. This happens when
							//	  you initiate the reload action and a map change occurs before it
							//	  is completed. In pyspades, sending a reload packet is implemented
							//	  by registering a callback function to the Twisted reactor. This
							//	  callback function sends a reload packet, but it does not check if
							//	  the current game round is finished, nor is it unregistered on a
							//	  map change.
							//
							//	  Such a reload packet would not (and should not) have any effect on
							//	  the current round. Also, an attempt to process it would result in
							//	  an "invalid player ID" exception, so we simply drop it during
							//	  map load sequence.
							//
							if (type == PacketTypeStateData) {
								status = NetClientStatusConnected;
								statusString = _Tr("NetClient", "Connected");

								try {
									MapLoaded();
								} catch (const std::exception& ex) {
									if (strstr(ex.what(), "File truncated") ||
										strstr(ex.what(), "EOF reached")) {
										SPLog("Map decoder returned error:\n%s", ex.what());
										Disconnect();
										statusString = _Tr("NetClient", "Error");
										throw;
									}
								} catch (...) {
									Disconnect();
									statusString = _Tr("NetClient", "Error");
									throw;
								}

								HandleGamePacket(reader);
							} else if (type == PacketTypeWeaponReload) {
								// Drop the reload packet. Pyspades does not
								// cancel the reload packets on map change and
								// they would cause an error if we would
								// process them
							} else {
								// Save the packet for later
								savedPackets.push_back(reader.GetData());
							}
						}
					}
				} else if (status == NetClientStatusConnected) {
					if (event.type == ENET_EVENT_TYPE_RECEIVE) {
						auto& reader = readerOrNone.value();

						try {
							HandleGamePacket(reader);
						} catch (const std::exception& ex) {
							int type = reader.GetType();
							reader.DumpDebug();
							SPRaise("Exception while handling packet type 0x%08x:\n%s", type, ex.what());
						}
					}
				}
			}
		}

		stmp::optional<World&> NetClient::GetWorld() { return client->GetWorld(); }

		stmp::optional<Player&> NetClient::GetPlayerOrNull(int pId) {
			SPADES_MARK_FUNCTION();
			if (!GetWorld())
				SPRaise("Invalid player ID %d: no world", pId);
			if (pId < 0 || pId >= static_cast<int>(GetWorld()->GetNumPlayerSlots()))
				return NULL;
			return GetWorld()->GetPlayer(pId);
		}
		Player& NetClient::GetPlayer(int pId) {
			SPADES_MARK_FUNCTION();
			if (!GetWorld())
				SPRaise("Invalid player ID %d: no world", pId);
			if (pId < 0 || pId >= static_cast<int>(GetWorld()->GetNumPlayerSlots()))
				SPRaise("Invalid player ID %d: out of range", pId);
			if (!GetWorld()->GetPlayer(pId))
				SPRaise("Invalid player ID %d: doesn't exist", pId);
			return GetWorld()->GetPlayer(pId).value();
		}

		stmp::optional<Player&> NetClient::GetLocalPlayerOrNull() {
			SPADES_MARK_FUNCTION();
			if (!GetWorld())
				SPRaise("Failed to get local player: no world");
			return GetWorld()->GetLocalPlayer();
		}
		Player& NetClient::GetLocalPlayer() {
			SPADES_MARK_FUNCTION();
			stmp::optional<Player&> maybePlayer = GetLocalPlayerOrNull();
			if (!maybePlayer)
				SPRaise("Failed to get local player: doesn't exist");
			return maybePlayer.value();
		}

		// Max chat packet is 255 bytes: type + playerId + chatType + msg → 252 msg bytes.
		static constexpr size_t kKickReasonMaxBytes = 252;

		/** Whether a world position falls inside the fixed Ace of Spades map volume.
		 * Used to reject nonsensical coordinates before they reach the renderer. */
		static bool IsInsideMapBounds(const Vector3& v) {
			return v.x >= 0.0F && v.x <= (float)GameMap::DefaultWidth &&
				   v.y >= 0.0F && v.y <= (float)GameMap::DefaultHeight &&
				   v.z >= 0.0F && v.z <= (float)GameMap::DefaultDepth;
		}

		void NetClient::CaptureKickReason(spades::client::NetPacketReader& r) {
			std::string msg = StripNewlines(TrimSpaces(r.ReadRemainingString()));
			customKickReasonString = msg.substr(0, kKickReasonMaxBytes);
		}

		std::string NetClient::DisconnectReasonString(uint32_t num) {
			if (!customKickReasonString.empty())
				return customKickReasonString;

			switch (num) {
				case 1: return _Tr("NetClient", "You are banned from this server.");
				case 2: return _Tr("NetClient", "Your network's public address has too many connections to this server.");
				case 3: return _Tr("NetClient", "Incompatible client protocol version.");
				case 4: return _Tr("NetClient", "Server full");
				case 5: return _Tr("NetClient", "Server shutdown");
				case 10: return _Tr("NetClient", "You were kicked from this server.");
				case 20: return _Tr("NetClient", "Invalid name");
				default: return _Tr("NetClient", "Unknown Reason");
			}
		}

		bool NetClient::HandleHandshakePackets(spades::client::NetPacketReader& r) {
			SPADES_MARK_FUNCTION();

			switch (r.GetType()) {
				case PacketTypeHandShakeInit: SendHandShakeValid(r.ReadInt()); return true;
				case PacketTypeExtensionInfo: HandleExtensionPacket(r); return true;
				// Handled here rather than in HandleGamePacket so the server may send a
				// Config as soon as the extension is negotiated: during the Connecting
				// stage anything but MapStart is treated as an unexpected packet, and
				// during a map transfer it would be parked until the world exists.
				case PacketTypeExtendedTeamplay: HandleExtendedTeamplayPacket(r); return true;
				case PacketTypeVersionGet: {
					if (r.GetNumRemainingBytes() > 0) {
						// Enhanced variant
						std::set<std::uint8_t> propertyIds;
						while (r.GetNumRemainingBytes())
							propertyIds.insert(r.ReadByte());
						SendVersionEnhanced(propertyIds);
					} else {
						// Simple variant
						SendVersion();
					}
				}
					return true;
				case PacketTypeChatMessage: {
					// A system kick (playerId 255, ChatTypeSystem) may arrive at any
					// connection stage. Consume it here so the Connecting
					// branch doesn't SPRaise on "unexpected packet" and the ReceivingMap
					// branch doesn't park it in savedPackets (which are only drained
					// after StateData, and StateData never arrives when the server kicks
					// before sending the map). Peek so non-kick chat falls through with
					// an untouched reader cursor.
					if (r.GetNumRemainingBytes() >= 2 &&
						r.Peek(0) == 255 && r.Peek(1) == ChatTypeSystem) {
						r.ReadByte(); // playerId
						r.ReadByte(); // chat type
						CaptureKickReason(r);
						return true;
					}
					// Any other chat before we're fully Connected has no World to
					// dispatch to and is not meaningful pre-game noise. Drop it.
					if (status != NetClientStatusConnected)
						return true;
					return false;
				}
				default: return false;
			}
		}

		void NetClient::HandleExtensionPacket(spades::client::NetPacketReader& r) {
			int extCount = r.ReadByte();
			for (int i = 0; i < extCount; i++) {
				int extId = r.ReadByte();
				int extVer = r.ReadByte();

				auto got = implementedExtensions.find(extId);
				if (got == implementedExtensions.end()) {
					SPLog("Client does not support extension %d v%d", extId, extVer);
				} else {
					SPLog("Client supports extension %d v%d", extId, extVer);
					extensions.emplace(got->first, got->second);
				}
			}

			SendSupportedExtensions();
		}

		void NetClient::HandleExtendedTeamplayPacket(spades::client::NetPacketReader& r) {
			SPADES_MARK_FUNCTION();

			// A server that never negotiated the extension has no business sending its
			// packets; ignoring them keeps the client's state defined by the handshake.
			if (!HasExtension(ExtensionTypeExtendedTeamplay)) {
				SPLog("Ignoring an Extended Teamplay packet from a server that did not "
					  "negotiate the extension");
				return;
			}

			switch (r.ReadByte()) { // sub packet id
				case ExtendedTeamplaySubConfig: {
					client->ExtendedTeamplayConfigured(r.ReadByte());
				} break;
				case ExtendedTeamplaySubPing: {
					int pId = r.ReadByte();
					Vector3 pos = r.ReadVector3();
					float duration = r.ReadFloat();
					uint8_t surfaces = r.ReadByte();

					// The marker colour, in the Blue-Green-Red order the base protocol
					// already uses. The server chose it; the client draws it as sent.
					IntVector3 color = r.ReadIntColor();

					uint8_t messageId = r.ReadByte();

					// The Reason occupies the rest of the packet, is plain UTF-8 rather
					// than the base protocol's Code Page 437, and may be empty.
					std::string reason =
					  ExtendedTeamplay::SanitizeReason(r.ReadRemainingData());

					// The Message ID is reserved and unimplemented: a value from a later
					// version is ignored rather than taken as a reason to drop a ping
					// that is otherwise perfectly renderable.
					if (messageId != ExtendedTeamplay::kReservedMessageId) {
						SPLog("Ignoring the message id %u on an Extended Teamplay ping",
							  (unsigned int)messageId);
					}

					if (!ExtendedTeamplay::IsValidDuration(duration)) {
						SPLog("Dropped an Extended Teamplay ping with an invalid duration");
						break;
					}

					// The server is authoritative on placement, but a NaN or a wildly
					// out-of-bounds position would corrupt the projection maths, so it
					// is rejected rather than drawn. A removal carries no place to check.
					if (duration != 0.0F && (pos.IsNaN() || !IsInsideMapBounds(pos))) {
						SPLog("Dropped an Extended Teamplay ping at an invalid position");
						break;
					}

					client->ExtendedTeamplayPingReceived(pId, pos, duration, surfaces,
														 color, std::move(reason));
				} break;
				case ExtendedTeamplaySubESPMark: {
					int pId = r.ReadByte();
					float duration = r.ReadFloat();
					uint8_t surfaces = r.ReadByte();
					uint8_t flags = r.ReadByte();

					// The outline colour, in the Blue-Green-Red order the base protocol
					// already uses. Black asks for the marked player's team colour.
					IntVector3 color = r.ReadIntColor();

					uint8_t messageId = r.ReadByte();

					std::string reason =
					  ExtendedTeamplay::SanitizeReason(r.ReadRemainingData());

					// Reserved and unimplemented, as on the ping: ignored, not fatal.
					if (messageId != ExtendedTeamplay::kReservedMessageId) {
						SPLog("Ignoring the message id %u on an Extended Teamplay mark",
							  (unsigned int)messageId);
					}

					if (!ExtendedTeamplay::IsValidDuration(duration)) {
						SPLog("Dropped an Extended Teamplay mark with an invalid duration");
						break;
					}

					client->ExtendedTeamplayMarkReceived(pId, duration, surfaces, flags,
														 color, std::move(reason));
				} break;
				default:
					// Sub packets are the extension's own versioning seam: an unknown one
					// belongs to a newer version and is skipped, not treated as an error.
					SPLog("Ignoring an unknown Extended Teamplay sub packet");
					break;
			}
		}

		void NetClient::HandleGamePacket(spades::client::NetPacketReader& r) {
			SPADES_MARK_FUNCTION();

			switch (r.GetType()) {
				case PacketTypePositionData: {
					Player& p = GetLocalPlayer();
					if (r.GetLength() != 13) {
						// sometimes 00 00 00 00 packet is sent.
						// ignore this now
						break;
					}
					p.RepositionPlayer(r.ReadVector3());
				} break;
				case PacketTypeOrientationData: {
					Player& p = GetLocalPlayer();

					// ignore invalid orientation
					Vector3 o = r.ReadVector3();
					if (o.GetSquaredLength() < 0.01F)
						break;

					o = o.Normalize();
					p.SetOrientation(o);
				} break;
				case PacketTypeWorldUpdate: {
					int bytesPerEntry = 24;
					if (protocolVersion == 4)
						bytesPerEntry++;

					client->MarkWorldUpdate();

					int entries = static_cast<int>(r.GetLength() / bytesPerEntry);
					for (int i = 0; i < entries; i++) {
						int idx = i;
						if (protocolVersion == 4) {
							idx = r.ReadByte();
							if (idx < 0 || idx >= properties->GetMaxNumPlayerSlots())
								SPRaise("Invalid player ID %d received with WorldUpdate", idx);
						}

						Vector3 pos = r.ReadVector3();
						Vector3 front = r.ReadVector3();

						{
							SPAssert(!pos.IsNaN());
							SPAssert(!front.IsNaN());
							SPAssert(front.GetLength() < 40.0F);

							if (GetWorld()) {
								auto p = GetWorld()->GetPlayer(idx);
								if (p && p != GetWorld()->GetLocalPlayer()
									&& p->IsAlive() && !p->IsSpectator()) {
									p->RepositionPlayer(pos);
									p->SetOrientation(front);
								}
							}
						}

						// save position and orientation
						savedPlayerPos.at(idx) = pos;
						savedPlayerFront.at(idx) = front;
					}
					SPAssert(r.ReadRemainingData().empty());
				} break;
				case PacketTypeInputData:
					if (!GetWorld())
						break;
					{
						Player& p = GetPlayer(r.ReadByte());
						PlayerInput inp = ParsePlayerInput(r.ReadByte());

						if (&p == GetWorld()->GetLocalPlayer()) {
							if (inp.jump) // handle "/fly" jump
								p.PlayerJump();

							break;
						}

						p.SetInput(inp);
					}
					break;
				case PacketTypeWeaponInput:
					if (!GetWorld())
						break;
					{
						Player& p = GetPlayer(r.ReadByte());
						WeaponInput inp = ParseWeaponInput(r.ReadByte());

						if (&p == GetWorld()->GetLocalPlayer())
							break;

						p.SetWeaponInput(inp);
					}
					break;
				case PacketTypeSetHP: { // Hit Packet is Client-to-Server!
					Player& p = GetLocalPlayer();
					int hp = r.ReadByte();
					int type = r.ReadByte(); // 0=fall, 1=weap
					Vector3 source = r.ReadVector3();
					p.SetHP(hp, type ? HurtTypeWeapon : HurtTypeFall, source);
				} break;
				case PacketTypeGrenadePacket:
					if (!GetWorld())
						break;
					{
						int pId = r.ReadByte();
						float fuse = r.ReadFloat();
						Vector3 pos = r.ReadVector3();
						Vector3 vel = r.ReadVector3();
						Grenade* g = new Grenade(*GetWorld(), pId, pos, vel, fuse);
						GetWorld()->AddGrenade(std::unique_ptr<Grenade>{g});
					}
					break;
				case PacketTypeSetTool: {
					Player& p = GetPlayer(r.ReadByte());
					int tool = r.ReadByte();
					p.SetTool(GetToolType(tool));
				} break;
				case PacketTypeSetColour: {
					stmp::optional<Player&> p = GetPlayerOrNull(r.ReadByte());
					IntVector3 color = r.ReadIntColor();
					if (p)
						p->SetHeldBlockColor(color);
					else
						temporaryPlayerBlockColor = color;
				} break;
				case PacketTypeExistingPlayer:
					if (!GetWorld())
						break;
					{
						int pId = r.ReadByte();
						int team = r.ReadByte();
						int weapon = r.ReadByte();
						int tool = r.ReadByte();
						int score = r.ReadInt();
						IntVector3 color = r.ReadIntColor(); // block color
						std::string name = StripNewlines(TrimSpaces(r.ReadRemainingString()));
						WeaponType wType = GetWeaponType(weapon);

						auto p = stmp::make_unique<Player>(*GetWorld(), pId, wType, team);

						// set position
						p->SetPosition(savedPlayerPos[pId]);

						// set block color
						p->SetHeldBlockColor(color);

						// set tool
						p->SetTool(GetToolType(tool));

						GetWorld()->SetPlayer(pId, std::move(p));

						// set name and score
						auto& pers = GetWorld()->GetPlayerPersistent(pId);
						pers.name = name;
						pers.score = score;

						savedPlayerTeam[pId] = team;
					}
					break;
				case PacketTypeShortPlayerData:
					SPRaise("Unexpected: received Short Player Data");
				case PacketTypeMoveObject: {
					if (!GetWorld())
						SPRaise("No world");

					int type = r.ReadByte();
					int state = r.ReadByte();
					Vector3 pos = r.ReadVector3();

					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (mode && mode->ModeType() == IGameMode::m_CTF) {
						auto& ctf = dynamic_cast<CTFGameMode&>(mode.value());

						CTFGameMode::Team& team1 = ctf.GetTeam(0);
						CTFGameMode::Team& team2 = ctf.GetTeam(1);

						switch (type) {
							case BLUE_FLAG: team1.flagPos = pos; break;
							case BLUE_BASE: team1.basePos = pos; break;
							case GREEN_FLAG: team2.flagPos = pos; break;
							case GREEN_BASE: team2.basePos = pos; break;
						}
					} else if (mode && mode->ModeType() == IGameMode::m_TC) {
						auto& tc = dynamic_cast<TCGameMode&>(mode.value());

						int numTerritories = tc.GetNumTerritories();
						if (type >= numTerritories) {
							SPRaise("Invalid territory id specified: %d (max = %d)",
								(int)type, numTerritories - 1);
						}

						if (state > 2)
							SPRaise("Invalid state %d specified for territory owner.", (int)state);

						TCGameMode::Territory& t = tc.GetTerritory(type);
						t.pos = pos;
						t.ownerTeamId = state;
					}
				} break;
				case PacketTypeCreatePlayer: {
					if (!GetWorld())
						SPRaise("No world");

					int pId = r.ReadByte();
					int weapon = r.ReadByte();
					int team = r.ReadByte();
					Vector3 pos = r.ReadVector3();
					std::string name = StripNewlines(TrimSpaces(r.ReadRemainingString()));

					if (pId < 0 || pId >= properties->GetMaxNumPlayerSlots()) {
						SPLog("Ignoring invalid player ID %d (pyspades bug?: %s)", pId, name.c_str());
						break;
					}

					WeaponType wType = GetWeaponType(weapon);

					auto p = stmp::make_unique<Player>(*GetWorld(), pId, wType, team);

					// adjust spawn height
					pos.z -= 2.4F;

					// set position
					p->SetPosition(pos);

					GetWorld()->SetPlayer(pId, std::move(p));

					// set name
					if (!name.empty()) // sometimes becomes empty
						GetWorld()->GetPlayerPersistent(pId).name = name;

					Player& pRef = GetWorld()->GetPlayer(pId).value();

					if (pId == GetWorld()->GetLocalPlayerIndex()) {
						client->LocalPlayerCreated();
						lastPlayerInput = 0xFFFFFFFF;
						lastWeaponInput = 0xFFFFFFFF;

						// override default block color for local player
						IntVector3 blockColor;
						blockColor.x = Clamp((int)cg_defaultBlockColorR, 0, 255);
						blockColor.y = Clamp((int)cg_defaultBlockColorG, 0, 255);
						blockColor.z = Clamp((int)cg_defaultBlockColorB, 0, 255);
						pRef.SetHeldBlockColor(blockColor);
						SendHeldBlockColor(); // ensure block color is synchronized
					}

					if (savedPlayerTeam[pId] != team) {
						client->PlayerJoinedTeam(pRef);
						savedPlayerTeam[pId] = team;
					}

					client->PlayerSpawned(pRef);
				} break;
				case PacketTypeBlockAction: {
					stmp::optional<Player&> p = GetPlayerOrNull(r.ReadByte());
					int action = r.ReadByte();
					IntVector3 pos = r.ReadIntVector3();

					std::vector<IntVector3> cells;
					if (action == BlockActionCreate) {
						if (!p) {
							GetWorld()->CreateBlock(pos, temporaryPlayerBlockColor);
						} else {
							GetWorld()->CreateBlock(pos, p->GetBlockColor());
							if (!GetWorld()->GetMap()->IsSolidWrapped(pos.x, pos.y, pos.z)) {
								p->UseBlocks(1);
								if (p->IsLocalPlayer())
									client->RegisterPlacedBlocks(1);
							}
							client->PlayerCreatedBlock(*p);
						}
					} else if (action == BlockActionTool) {
						cells.push_back(pos);
						GetWorld()->DestroyBlock(cells);
						if (p && p->IsToolSpade())
							p->GotBlock();
						client->PlayerDestroyedBlockWithWeaponOrTool(pos);
					} else if (action == BlockActionDig) {
						for (int z = -1; z <= 1; z++)
							cells.push_back(MakeIntVector3(pos.x, pos.y, pos.z + z));
						GetWorld()->DestroyBlock(cells);
						client->PlayerDiggedBlock(pos);
					} else if (action == BlockActionGrenade) {
						for (int x = -1; x <= 1; x++)
						for (int y = -1; y <= 1; y++)
						for (int z = -1; z <= 1; z++)
							cells.push_back(MakeIntVector3(pos.x + x, pos.y + y, pos.z + z));
						GetWorld()->DestroyBlock(cells);
						client->GrenadeDestroyedBlock(pos);
					}
				} break;
				case PacketTypeBlockLine: {
					stmp::optional<Player&> p = GetPlayerOrNull(r.ReadByte());

					IntVector3 pos1, pos2;
					pos1 = r.ReadIntVector3();
					pos2 = r.ReadIntVector3();

					auto cells = GetWorld()->CubeLine(pos1, pos2, 50);
					for (const auto& c : cells) {
						if (!GetWorld()->GetMap()->IsSolid(c.x, c.y, c.z))
							GetWorld()->CreateBlock(c, p ? p->GetBlockColor()
														 : temporaryPlayerBlockColor);
					}

					if (p) {
						int blocks = static_cast<int>(cells.size());
						p->UseBlocks(blocks);
						if (p->IsLocalPlayer())
							client->RegisterPlacedBlocks(blocks);
						client->PlayerCreatedBlock(*p);
					}
				} break;
				case PacketTypeStateData:
					if (!GetWorld())
						break;
					{
						// receives my player info.
						int pId = r.ReadByte();
						IntVector3 fogColor = r.ReadIntColor();

						IntVector3 teamColors[2];
						teamColors[0] = r.ReadIntColor();
						teamColors[1] = r.ReadIntColor();

						std::string teamNames[2];
						teamNames[0] = r.ReadString(10);
						teamNames[1] = r.ReadString(10);

						World::Team& t1 = GetWorld()->GetTeam(0);
						World::Team& t2 = GetWorld()->GetTeam(1);
						t1.color = teamColors[0];
						t2.color = teamColors[1];
						t1.name = teamNames[0];
						t2.name = teamNames[1];

						GetWorld()->SetFogColor(fogColor);
						GetWorld()->SetLocalPlayerIndex(pId);

						int mode = r.ReadByte();
						if (mode == CTFGameMode::m_CTF) { // CTF
							auto ctf = stmp::make_unique<CTFGameMode>();

							CTFGameMode::Team& team1 = ctf->GetTeam(0);
							CTFGameMode::Team& team2 = ctf->GetTeam(1);

							team1.score = r.ReadByte();
							team2.score = r.ReadByte();
							ctf->SetCaptureLimit(r.ReadByte());

							int intelFlags = r.ReadByte();
							team1.hasIntel = (intelFlags & 1) != 0;
							team2.hasIntel = (intelFlags & 2) != 0;

							if (team2.hasIntel) {
								team2.carrierId = r.ReadByte();
								r.ReadData(11);
							} else {
								team1.flagPos = r.ReadVector3();
							}

							if (team1.hasIntel) {
								team1.carrierId = r.ReadByte();
								r.ReadData(11);
							} else {
								team2.flagPos = r.ReadVector3();
							}

							team1.basePos = r.ReadVector3();
							team2.basePos = r.ReadVector3();

							GetWorld()->SetMode(std::move(ctf));
						} else { // TC
							auto tc = stmp::make_unique<TCGameMode>(*GetWorld());

							int trNum = r.ReadByte();
							for (int i = 0; i < trNum; i++) {
								TCGameMode::Territory t{*tc};
								t.pos = r.ReadVector3();

								int state = r.ReadByte();
								t.ownerTeamId = state;
								t.progressBasePos = 0.0F;
								t.progressStartTime = 0.0F;
								t.progressRate = 0.0F;
								t.capturingTeamId = -1;
								tc->AddTerritory(t);
							}

							GetWorld()->SetMode(std::move(tc));
						}
						client->JoinedGame();
					}
					break;
				case PacketTypeKillAction: {
					int victimId = r.ReadByte();
					int killerId = r.ReadByte();
					int kt = r.ReadByte();
					int respawnTime = r.ReadByte();

					KillType type;
					switch (kt) {
						case 0: type = KillTypeWeapon; break;
						case 1: type = KillTypeHeadshot; break;
						case 2: type = KillTypeMelee; break;
						case 3: type = KillTypeGrenade; break;
						case 4: type = KillTypeFall; break;
						case 5: type = KillTypeTeamChange; break;
						case 6: type = KillTypeClassChange; break;
						default: SPInvalidEnum("kt", kt);
					}
					switch (type) {
						case KillTypeFall:
						case KillTypeTeamChange:
						case KillTypeClassChange: killerId = victimId; break;
						default: break;
					}

					Player& victim = GetPlayer(victimId);
					Player& killer = GetPlayer(killerId);
					victim.KilledBy(type, killer, respawnTime);
					if (killerId != victimId)
						GetWorld()->GetPlayerPersistent(killerId).score++;
				} break;
				case PacketTypeChatMessage: {
					// might be wrong player id for server message
					int playerId = r.ReadByte();
					int type = r.ReadByte();
					std::string msg = StripNewlines(TrimSpaces(r.ReadRemainingString()));

					if (type == ChatTypeSystem) {
						// playerId 255 is the kick-reason channel, already consumed by
						// HandleHandshakePackets before we get here.
						client->ServerSentMessage(false, msg);

						// Speculate the best game properties based on the server generated messages
						properties->HandleServerMessage(msg);
					} else if (type == ChatTypeAll || type == ChatTypeTeam) {
						stmp::optional<Player&> p = GetPlayerOrNull(playerId);
						if (p) {
							client->PlayerSentChatMessage(*p, (type == ChatTypeAll), msg);
						} else {
							client->ServerSentMessage((type == ChatTypeTeam), msg);
						}
					} else if (type == ChatTypeBig) {
						client->ServerSentMessage(false, CHATPREFIX_BIG + msg);
					} else if (type == ChatTypeInfo) {
						client->ServerSentMessage(false, CHATPREFIX_NOTICE + msg);
					} else if (type == ChatTypeWarning) {
						client->ServerSentMessage(false, CHATPREFIX_WARNING + msg);
					} else if (type == ChatTypeError) {
						client->ServerSentMessage(false, CHATPREFIX_ERROR + msg);
					} else if (type == ChatTypeDirect &&
							   HasExtension(ExtensionTypeExtendedTeamplay)) {
						// The packet needs no field for the recipient — it is us — and
						// carries the sender in its own player id, 255 for the server.
						stmp::optional<Player&> p = GetPlayerOrNull(playerId);
						client->ServerSentDirectMessage(p ? (*p).GetName() : std::string(), msg);
					}
				} break;
				case PacketTypeMapStart: {
					// next map!
					if (protocolVersion == 4)
						SendMapCached();

					client->SetWorld(NULL);

					auto mapSize = r.ReadInt();
					SPLog("Map size advertised by the server: %lu", (unsigned long)mapSize);

					mapLoader.reset(new GameMapLoader());
					mapLoadMonitor.reset(new MapDownloadMonitor(*mapLoader));

					status = NetClientStatusReceivingMap;
					statusString = _Tr("NetClient", "Loading snapshot");
				} break;
				case PacketTypeMapChunk: SPRaise("Unexpected: received Map Chunk while game");
				case PacketTypePlayerLeft: {
					int pId = r.ReadByte();
					Player& p = GetPlayer(pId);

					client->PlayerLeaving(p);
					GetWorld()->GetPlayerPersistent(pId).score = 0;

					savedPlayerTeam[pId] = -1;
					GetWorld()->SetPlayer(pId, NULL);
				} break;
				case PacketTypeTerritoryCapture: {
					int territoryId = r.ReadByte();
					bool winning = r.ReadByte() != 0;
					int state = r.ReadByte();

					// TODO: This piece is repeated for at least three times
					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode) {
						SPLog("Ignoring PacketTypeTerritoryCapture"
							  "because game mode isn't specified yet");
						break;
					}
					if (mode->ModeType() != IGameMode::m_TC)
						SPRaise("Received PacketTypeTerritoryCapture in non-TC gamemode");

					auto& tc = dynamic_cast<TCGameMode&>(*mode);

					int numTerritories = tc.GetNumTerritories();
					if (territoryId >= numTerritories) {
						SPRaise("Invalid territory id %d specified (max = %d)",
							territoryId, numTerritories - 1);
					}

					client->TeamCapturedTerritory(state, territoryId);

					TCGameMode::Territory& t = tc.GetTerritory(territoryId);
					t.ownerTeamId = state;
					t.progressBasePos = 0.0F;
					t.progressRate = 0.0F;
					t.progressStartTime = 0.0F;
					t.capturingTeamId = -1;

					if (winning)
						client->TeamWon(state);
				} break;
				case PacketTypeProgressBar: {
					int territoryId = r.ReadByte();
					int capturingTeam = r.ReadByte();
					int rate = (int8_t)r.ReadByte();
					float progress = r.ReadFloat();

					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode) {
						SPLog("Ignoring PacketTypeProgressBar"
							  "because game mode isn't specified yet");
						break;
					}
					if (mode->ModeType() != IGameMode::m_TC)
						SPRaise("Received PacketTypeProgressBar in non-TC gamemode");

					auto& tc = dynamic_cast<TCGameMode&>(*mode);

					int numTerritories = tc.GetNumTerritories();
					if (territoryId >= numTerritories) {
						SPRaise("Invalid territory id %d specified (max = %d)",
							territoryId, numTerritories - 1);
					}

					if (progress < -0.1F || progress > 1.1F)
						SPRaise("Progress value out of range(%f)", progress);

					TCGameMode::Territory& t = tc.GetTerritory(territoryId);
					t.progressBasePos = progress;
					t.progressRate = (float)rate * TC_CAPTURE_RATE;
					t.progressStartTime = GetWorld()->GetTime();
					t.capturingTeamId = capturingTeam;
				} break;
				case PacketTypeIntelCapture: {
					if (!GetWorld())
						SPRaise("No world");

					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode) {
						SPLog("Ignoring PacketTypeIntelCapture"
							  "because game mode isn't specified yet");
						break;
					}
					if (mode->ModeType() != IGameMode::m_CTF)
						SPRaise("Received PacketTypeIntelCapture in non-CTF gamemode");

					int pId = r.ReadByte();
					Player& p = GetPlayer(pId);
					int teamId = p.GetTeamId();

					auto& ctf = dynamic_cast<CTFGameMode&>(mode.value());
					CTFGameMode::Team& team = ctf.GetTeam(teamId);
					team.score++;
					team.hasIntel = false;

					client->PlayerCapturedIntel(p);
					GetWorld()->GetPlayerPersistent(pId).score += 10;

					bool winning = r.ReadByte() != 0;
					if (winning) {
						client->TeamWon(teamId);
						ctf.ResetIntelHoldingStatus();
					}
				} break;
				case PacketTypeIntelPickup: {
					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode) {
						SPLog("Ignoring PacketTypeIntelPickup"
							  "because game mode isn't specified yet");
						break;
					}
					if (mode->ModeType() != IGameMode::m_CTF)
						SPRaise("Received PacketTypeIntelPickup in non-CTF gamemode");

					int pId = r.ReadByte();
					Player& p = GetPlayer(pId);

					auto& ctf = dynamic_cast<CTFGameMode&>(mode.value());
					CTFGameMode::Team& team = ctf.GetTeam(p.GetTeamId());
					team.hasIntel = true;
					team.carrierId = pId;
					client->PlayerPickedIntel(p);
				} break;
				case PacketTypeIntelDrop: {
					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode) {
						SPLog("Ignoring PacketTypeIntelDrop"
							  "because game mode isn't specified yet");
						break;
					}
					if (mode->ModeType() != IGameMode::m_CTF)
						SPRaise("Received PacketTypeIntelDrop in non-CTF gamemode");

					Player& p = GetPlayer(r.ReadByte());
					int teamId = p.GetTeamId();

					auto& ctf = dynamic_cast<CTFGameMode&>(mode.value());
					ctf.GetTeam(teamId).hasIntel = false;
					ctf.GetTeam(1 - teamId).flagPos = r.ReadVector3();
					client->PlayerDropIntel(p);
				} break;
				case PacketTypeRestock: {
					r.ReadByte(); // skip player id
					Player& p = GetLocalPlayer();
					p.Restock();
				} break;
				case PacketTypeFogColour: {
					if (GetWorld()) {
						r.ReadByte(); // skip alpha value
						GetWorld()->SetFogColor(r.ReadIntColor());
					}
				} break;
				case PacketTypeWeaponReload: {
					Player& p = GetPlayer(r.ReadByte());
					if (&p != GetLocalPlayerOrNull()) {
						p.Reload();
					} else {
						int clip = r.ReadByte();
						int reserve = r.ReadByte();
						p.ReloadDone(clip, reserve);
					}
				} break;
				case PacketTypeChangeTeam: {
					r.ReadByte(); // skip player id
					r.ReadByte(); // skip team id

					/*
						Player& p = GetPlayer(pId);
						if (team < 0 || team > 2)
							SPRaise("Received invalid team: %d", team);
						p.SetTeam(team);
					*/
				} break;
				case PacketTypeChangeWeapon: {
					r.ReadByte(); // skip player id
					r.ReadByte(); // skip weapon id

					/*
						Player& p = GetPlayer(pId);
						WeaponType wType = GetWeaponType(weapon);
						p.SetWeaponType(wType);
					*/
				} break;
				case PacketTypePlayerProperties: {
					r.ReadByte(); // skip subId
					int pId = r.ReadByte();
					int hp = r.ReadByte();
					int blocks = r.ReadByte();
					int grenades = r.ReadByte();
					int clip = r.ReadByte();
					int reserve = r.ReadByte();
					int score = r.ReadInt();

					Player& p = GetPlayer(pId);
					Weapon& w = p.GetWeapon();

					if (pId == GetWorld()->GetLocalPlayerIndex())
						p.Restock(hp, grenades, blocks);
					w.Restock(clip, reserve);
					GetWorld()->GetPlayerPersistent(pId).score = score;
				} break;
				default:
					printf("WARNING: dropped packet %d\n", (int)r.GetType());
					r.DumpDebug();
			}
		}

		void NetClient::SendVersionEnhanced(const std::set<std::uint8_t>& propertyIds) {
			NetPacketWriter w(PacketTypeExistingPlayer);
			w.WriteByte((uint8_t)'x');

			for (std::uint8_t propertyId : propertyIds) {
				w.WriteByte(propertyId);

				auto lengthLabel = w.GetPosition();
				w.WriteByte((uint8_t)0); // dummy data for "Payload Length"

				auto beginLabel = w.GetPosition();
				switch (static_cast<VersionInfoPropertyId>(propertyId)) {
					case VersionInfoPropertyId::ApplicationNameAndVersion:
						w.WriteByte((uint8_t)OPENSPADES_VERSION_MAJOR);
						w.WriteByte((uint8_t)OPENSPADES_VERSION_MINOR);
						w.WriteByte((uint8_t)OPENSPADES_VERSION_PATCH);
						w.WriteString("OpenSpades");
						break;
					case VersionInfoPropertyId::UserLocale:
						w.WriteString(GetCurrentLocaleAndRegion());
						break;
					case VersionInfoPropertyId::ClientFeatureFlags1: {
						auto flags = ClientFeatureFlags1::None;
						if (cg_unicode)
							flags |= ClientFeatureFlags1::SupportsUnicode;
						w.WriteInt(static_cast<uint32_t>(flags));
					} break;
					default:
						// Just return empty payload for an unrecognized property
						break;
				}

				w.Update(lengthLabel, (uint8_t)(w.GetPosition() - beginLabel));
				enet_peer_send(peer, 0, w.CreatePacket());
			}
		}

		void NetClient::SendJoin(int team, WeaponType weapType, std::string name, int score) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeExistingPlayer);
			w.WriteByte((uint8_t)0); // Player ID, but shouldn't matter here
			w.WriteByte((uint8_t)team);
			w.WriteByte((uint8_t)GetWeaponId(weapType));
			w.WriteByte((uint8_t)2); // TODO: change tool
			w.WriteInt((uint32_t)score);
			w.WriteColor(GetWorld()->GetTeamColor(team));
			w.WriteString(name, 16);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendPosition(spades::Vector3 v) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypePositionData);
			w.WriteVector3(v);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendOrientation(spades::Vector3 v) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeOrientationData);
			w.WriteVector3(v);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendPlayerInput(PlayerInput inp) {
			SPADES_MARK_FUNCTION();

			uint8_t bits =
				inp.moveForward << 0 |
				inp.moveBackward << 1 |
				inp.moveLeft << 2 |
				inp.moveRight << 3 |
				inp.jump << 4 |
				inp.crouch << 5 |
				inp.sneak << 6 |
				inp.sprint << 7;

			if ((unsigned int)bits == lastPlayerInput)
				return;

			lastPlayerInput = bits;

			NetPacketWriter w(PacketTypeInputData);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteByte(bits);

			// Record to demo before sending (server doesn't echo this back)
			if (demoRecorder && demoRecorder->IsRecording()) {
				const auto& data = w.GetData();
				demoRecorder->RecordPacket(data.data(), data.size());
			}

			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendWeaponInput(WeaponInput inp) {
			SPADES_MARK_FUNCTION();

			uint8_t bits = inp.primary << 0 | inp.secondary << 1;

			if ((unsigned int)bits == lastWeaponInput)
				return;

			lastWeaponInput = bits;

			NetPacketWriter w(PacketTypeWeaponInput);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteByte(bits);

			// Record to demo before sending (server doesn't echo this back)
			if (demoRecorder && demoRecorder->IsRecording()) {
				const auto& data = w.GetData();
				demoRecorder->RecordPacket(data.data(), data.size());
			}

			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendHit(int targetPlayerId, HitType type) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeHitPacket);
			w.WriteByte((uint8_t)targetPlayerId);
			switch (type) {
				case HitTypeTorso: w.WriteByte((uint8_t)0); break;
				case HitTypeHead: w.WriteByte((uint8_t)1); break;
				case HitTypeArms: w.WriteByte((uint8_t)2); break;
				case HitTypeLegs: w.WriteByte((uint8_t)3); break;
				case HitTypeMelee: w.WriteByte((uint8_t)4); break;
				default: SPInvalidEnum("type", type);
			}
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendGrenade(const Grenade& g) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeGrenadePacket);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteFloat(g.GetFuse());
			w.WriteVector3(g.GetPosition());
			w.WriteVector3(g.GetVelocity());

			// Record to demo before sending (server doesn't echo this back)
			if (demoRecorder && demoRecorder->IsRecording()) {
				const auto& data = w.GetData();
				demoRecorder->RecordPacket(data.data(), data.size());
			}

			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendTool() {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeSetTool);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteByte((uint8_t)GetToolId(GetLocalPlayer().GetTool()));

			// Record to demo before sending (server doesn't echo this back)
			if (demoRecorder && demoRecorder->IsRecording()) {
				const auto& data = w.GetData();
				demoRecorder->RecordPacket(data.data(), data.size());
			}

			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendHeldBlockColor() {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeSetColour);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteColor(GetLocalPlayer().GetBlockColor());

			// Record to demo before sending (server doesn't echo this back)
			if (demoRecorder && demoRecorder->IsRecording()) {
				const auto& data = w.GetData();
				demoRecorder->RecordPacket(data.data(), data.size());
			}

			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendBlockAction(spades::IntVector3 v, BlockActionType type) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeBlockAction);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			switch (type) {
				case BlockActionCreate: w.WriteByte((uint8_t)0); break;
				case BlockActionTool: w.WriteByte((uint8_t)1); break;
				case BlockActionDig: w.WriteByte((uint8_t)2); break;
				case BlockActionGrenade: w.WriteByte((uint8_t)3); break;
				default: SPInvalidEnum("type", type);
			}
			w.WriteIntVector3(v);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendBlockLine(spades::IntVector3 v1, spades::IntVector3 v2) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeBlockLine);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteIntVector3(v1);
			w.WriteIntVector3(v2);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendChat(std::string text, bool global) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeChatMessage);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteByte((uint8_t)(global ? 0 : 1));
			w.WriteString(text);
			w.WriteByte((uint8_t)0);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendReload() {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeWeaponReload);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteByte((uint8_t)0); // clip_ammo; not used?
			w.WriteByte((uint8_t)0); // reserve_ammo; not used?

			// Record to demo before sending (server response is delayed)
			if (demoRecorder && demoRecorder->IsRecording()) {
				const auto& data = w.GetData();
				demoRecorder->RecordPacket(data.data(), data.size());
			}

			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendTeamChange(int team) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeChangeTeam);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteByte((uint8_t)team);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendWeaponChange(WeaponType type) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeChangeWeapon);
			w.WriteByte((uint8_t)GetLocalPlayer().GetId());
			w.WriteByte((uint8_t)GetWeaponId(type));
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendMapCached() {
			SPADES_MARK_FUNCTION();

			// The AoS 0.76 protocol allows the client to load a map from a local cache
			// if possible. After receiving MapStart, the client should respond with
			// MapCached to indicate whether the map with a given checksum exists in the
			// cache or not. We didn't implement a local cache, so we always ask the
			// server to send fresh map data.
			NetPacketWriter w(PacketTypeMapCached);
			w.WriteByte((uint8_t)0);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendHandShakeValid(int challenge) {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeHandShakeReturn);
			w.WriteInt((uint32_t)challenge);

			SPLog("Sending hand shake back.");
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendVersion() {
			SPADES_MARK_FUNCTION();

			std::string osInfo = VersionInfo::GetVersionInfo();
			std::string archInfo = VersionInfo::GetAppArchitecture();

			osInfo.append(" | " ZEROSPADES_VER_STR);
			osInfo.append(" (" + archInfo + ")");

			NetPacketWriter w(PacketTypeVersionSend);
			w.WriteByte((uint8_t)'o');
			w.WriteByte((uint8_t)OPENSPADES_VERSION_MAJOR);
			w.WriteByte((uint8_t)OPENSPADES_VERSION_MINOR);
			w.WriteByte((uint8_t)OPENSPADES_VERSION_PATCH);
			w.WriteString(osInfo);

			SPLog("Sending version back.");
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendTeamplayPing(Vector3 position, const std::string& reason) {
			SPADES_MARK_FUNCTION();

			// The caller is expected to have checked the feature bits, but the
			// negotiation is this class's own business, so it is enforced here.
			if (!HasExtension(ExtensionTypeExtendedTeamplay))
				return;

			NetPacketWriter w(PacketTypeExtendedTeamplay);
			w.WriteByte((uint8_t)ExtendedTeamplaySubPing);

			// The Player ID is ignored in this direction; the server fills it in
			// authoritatively. Sent as 255 so a server reading it sees "unset" rather
			// than a plausible-looking impersonation of some other player.
			w.WriteByte((uint8_t)ExtendedTeamplay::kServerPlayerId);

			w.WriteVector3(position);

			// How long the ping lasts is the server's call alone, so a client sends `0`
			// and the server fills in what it thinks the ping is worth. Where it is
			// shown is the server's call too, and `0` asks it to decide.
			w.WriteFloat(0.0F);
			w.WriteByte((uint8_t)0);

			// Only the server sets a ping's colour, so these three go out empty and are
			// ignored on arrival like the two fields above them.
			w.WriteColor(MakeIntVector3(0, 0, 0));

			w.WriteByte(ExtendedTeamplay::kReservedMessageId);

			// The Reason is UTF-8 text, not the base protocol's Code Page 437, so the
			// bytes go out as they are.
			w.WriteRawString(ExtendedTeamplay::SanitizeReason(reason));

			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendSupportedExtensions() {
			SPADES_MARK_FUNCTION();

			NetPacketWriter w(PacketTypeExtensionInfo);
			w.WriteByte(static_cast<uint8_t>(extensions.size()));
			for (const auto& i : extensions) {
				w.WriteByte(static_cast<uint8_t>(i.first));	 // ext id
				w.WriteByte(static_cast<uint8_t>(i.second)); // ext version
			}

			SPLog("Sending extension support.");
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::MapLoaded() {
			SPADES_MARK_FUNCTION();

			SPAssert(mapLoader);

			// Move `mapLoader` to a local variable so that the associated resources
			// are released as soon as possible when no longer needed
			std::unique_ptr<GameMapLoader> mapLoader = std::move(this->mapLoader);
			mapLoadMonitor.reset();

			SPLog("Waiting for the game map decoding to complete...");
			mapLoader->MarkEOF();
			mapLoader->WaitComplete();
			GameMap* map = mapLoader->TakeGameMap().Unmanage();
			SPLog("The game map was decoded successfully.");

			// now initialize world
			World* w = new World(properties);
			w->SetMap(map);
			map->Release();
			SPLog("World initialized.");

			client->SetWorld(w);

			SPAssert(GetWorld());

			SPLog("World loaded. Processing saved packets (%d)...", (int)savedPackets.size());

			std::fill(savedPlayerTeam.begin(), savedPlayerTeam.end(), -1);

			// do saved packets
			try {
				for (const auto& packets : savedPackets) {
					NetPacketReader r(packets);
					HandleGamePacket(r);
				}
				savedPackets.clear();
				SPLog("Done.");
			} catch (...) {
				savedPackets.clear();
				throw;
			}
		}

		float NetClient::GetMapReceivingProgress() {
			SPAssert(status == NetClientStatusReceivingMap);

			return mapLoader->GetProgress();
		}

		std::string NetClient::GetStatusString() {
			if (status == NetClientStatusReceivingMap) {
				// Display extra information
				auto text = mapLoadMonitor->GetDisplayedText();
				if (!text.empty())
					return Format("{0} ({1})", statusString, text);
			}

			return statusString;
		}

		NetClient::BandwidthMonitor::BandwidthMonitor(ENetHost* host)
			: host(host), lastDown(0.0), lastUp(0.0) {
			sw.Reset();
		}

		void NetClient::BandwidthMonitor::Update() {
			if (sw.GetTime() > 0.5) {
				lastUp = host->totalSentData / sw.GetTime();
				lastDown = host->totalReceivedData / sw.GetTime();
				host->totalSentData = 0;
				host->totalReceivedData = 0;
				sw.Reset();
			}
		}

		NetClient::MapDownloadMonitor::MapDownloadMonitor(GameMapLoader& mapLoader)
			: numBytesDownloaded{0}, mapLoader{mapLoader}, receivedFirstByte{false} {}

		void NetClient::MapDownloadMonitor::AccumulateBytes(unsigned int numBytes) {
			// It might take a while before receiving the first byte. Take this into account to
			// get a more accurate estimate of download time.
			if (!receivedFirstByte) {
				sw.Reset();
				receivedFirstByte = true;
			}

			numBytesDownloaded += numBytes;
		}

		std::string NetClient::MapDownloadMonitor::GetDisplayedText() {
			if (!receivedFirstByte)
				return {};

			float secsElapsed = static_cast<float>(sw.GetTime());
			if (secsElapsed <= 0.0F)
				return {};

			float progress = mapLoader.GetProgress();
			float bytesPerSec = static_cast<float>(numBytesDownloaded) / secsElapsed;
			float progressPerSec = progress / secsElapsed;

			std::string text = Format("{0} KB, {1} KB/s",
				(numBytesDownloaded + 500) / 1000, ((int)bytesPerSec + 500) / 1000);

			// Estimate the remaining time
			float secondsRemaining = (1.0F - progress) / progressPerSec;
			if (secondsRemaining < 86400.0F) {
				int seconds = (int)secondsRemaining + 1;

				text += ", ";
				if (seconds < 120)
					text += _TrN("NetClient", "{0} second remaining",
						"{0} seconds remaining", seconds);
				else
					text += _TrN("NetClient", "{0} minute remaining",
						"{0} minutes remaining", seconds / 60);
			}

			return text;
		}

		void NetClient::WriteInitialDemoState() {
			SPADES_MARK_FUNCTION();

			if (!demoRecorder || !demoRecorder->IsRecording())
				return;

			World* world = GetWorld() ? &GetWorld().value() : nullptr;
			if (!world) {
				SPLog("Cannot write initial demo state: no world");
				return;
			}

			GameMap* map = world->GetMap().GetPointerOrNull();
			if (!map) {
				SPLog("Cannot write initial demo state: no map");
				return;
			}

			SPLog("Writing initial demo state...");

			// compress and write map data
			{
				// Save map to memory stream
				DynamicMemoryStream rawMapStream;
				map->Save(&rawMapStream);
				rawMapStream.SetPosition(0);

				// Compress the map data
				DynamicMemoryStream compressedStream;
				{
					DeflateStream deflate(&compressedStream, CompressModeCompress, false);
					const size_t bufSize = 65536;
					std::vector<char> buf(bufSize);
					size_t read;
					while ((read = rawMapStream.Read(buf.data(), bufSize)) > 0) {
						deflate.Write(buf.data(), read);
					}
					deflate.DeflateEnd();
				}
				compressedStream.SetPosition(0);
				size_t compressedSize = compressedStream.GetLength();

				// Write MapStart packet
				{
					NetPacketWriter w(PacketTypeMapStart);
					w.WriteInt(static_cast<uint32_t>(compressedSize));
					const auto& data = w.GetData();
					demoRecorder->RecordPacket(data.data(), data.size());
				}

				// Write MapChunk packets (8KB chunks like the server does)
				const size_t chunkSize = 8192;
				std::vector<char> chunkBuf(chunkSize + 1);
				chunkBuf[0] = static_cast<char>(PacketTypeMapChunk);
				size_t read;
				while ((read = compressedStream.Read(chunkBuf.data() + 1, chunkSize)) > 0) {
					demoRecorder->RecordPacket(chunkBuf.data(), read + 1);
				}
			}

			// write StateData packet
			{
				NetPacketWriter w(PacketTypeStateData);

				// Local player ID
				int localPlayerId = world->GetLocalPlayerIndex().value_or(0);
				w.WriteByte(static_cast<uint8_t>(localPlayerId));

				// Fog color (BGR format)
				w.WriteColor(world->GetFogColor());

				// Team colors and names
				for (int t = 0; t < 2; t++) {
					w.WriteColor(world->GetTeam(t).color);
				}
				for (int t = 0; t < 2; t++) {
					w.WriteString(world->GetTeam(t).name, 10);
				}

				// Game mode
				stmp::optional<IGameMode&> mode = world->GetMode();
				if (mode && mode->ModeType() == IGameMode::m_CTF) {
					auto& ctf = dynamic_cast<CTFGameMode&>(*mode);
					w.WriteByte(0); // CTF mode

					CTFGameMode::Team& team1 = ctf.GetTeam(0);
					CTFGameMode::Team& team2 = ctf.GetTeam(1);

					w.WriteByte(static_cast<uint8_t>(team1.score));
					w.WriteByte(static_cast<uint8_t>(team2.score));
					w.WriteByte(static_cast<uint8_t>(ctf.GetCaptureLimit()));

					int intelFlags = (team1.hasIntel ? 1 : 0) | (team2.hasIntel ? 2 : 0);
					w.WriteByte(static_cast<uint8_t>(intelFlags));

					// Team 2's intel (blue team's flag)
					if (team2.hasIntel) {
						w.WriteByte(static_cast<uint8_t>(team2.carrierId));
						// Padding
						for (int i = 0; i < 11; i++) w.WriteByte(0);
					} else {
						w.WriteVector3(team1.flagPos);
					}

					// Team 1's intel (green team's flag)
					if (team1.hasIntel) {
						w.WriteByte(static_cast<uint8_t>(team1.carrierId));
						// Padding
						for (int i = 0; i < 11; i++) w.WriteByte(0);
					} else {
						w.WriteVector3(team2.flagPos);
					}

					// Base positions
					w.WriteVector3(team1.basePos);
					w.WriteVector3(team2.basePos);
				} else if (mode && mode->ModeType() == IGameMode::m_TC) {
					auto& tc = dynamic_cast<TCGameMode&>(*mode);
					w.WriteByte(1); // TC mode

					int numTerritories = tc.GetNumTerritories();
					w.WriteByte(static_cast<uint8_t>(numTerritories));
					for (int i = 0; i < numTerritories; i++) {
						TCGameMode::Territory& t = tc.GetTerritory(i);
						w.WriteVector3(t.pos);
						w.WriteByte(static_cast<uint8_t>(t.ownerTeamId));
					}
				} else {
					// Default to CTF with empty state
					w.WriteByte(0);
					for (int i = 0; i < 52; i++) w.WriteByte(0);
				}

				const auto& data = w.GetData();
				demoRecorder->RecordPacket(data.data(), data.size());
			}

			// write ExistingPlayer packets for all players
			for (unsigned int i = 0; i < world->GetNumPlayerSlots(); i++) {
				stmp::optional<Player&> maybePlayer = world->GetPlayer(i);
				if (!maybePlayer)
					continue;

				Player& p = *maybePlayer;

				NetPacketWriter w(PacketTypeExistingPlayer);
				w.WriteByte(static_cast<uint8_t>(i)); // player ID
				w.WriteByte(static_cast<uint8_t>(p.GetTeamId())); // team
				w.WriteByte(static_cast<uint8_t>(GetWeaponId(p.GetWeaponType()))); // weapon
				w.WriteByte(static_cast<uint8_t>(GetToolId(p.GetTool()))); // tool
				w.WriteInt(static_cast<uint32_t>(world->GetPlayerPersistent(i).score)); // kill count (score)
				w.WriteColor(p.GetBlockColor()); // block color
				w.WriteString(world->GetPlayerPersistent(i).name); // name

				const auto& data = w.GetData();
				demoRecorder->RecordPacket(data.data(), data.size());
			}

			SPLog("Initial demo state written successfully");
		}

		bool NetClient::StartDemoRecording(const std::string& filename, const std::string& context) {
			SPADES_MARK_FUNCTION();

			if (!demoRecorder) {
				SPLog("Demo recorder not initialized");
				return false;
			}

			if (status != NetClientStatusConnected) {
				SPLog("Cannot start demo recording: not connected to a server");
				return false;
			}

			std::string fname = filename.empty() ? DemoRecorder::GenerateFilename(context) : filename;
			if (!demoRecorder->StartRecording(fname, protocolVersion))
				return false;

			// Write initial game state (map, players, etc.) to the demo
			WriteInitialDemoState();

			return true;
		}

		void NetClient::StopDemoRecording() {
			SPADES_MARK_FUNCTION();

			if (demoRecorder && demoRecorder->IsRecording())
				demoRecorder->StopRecording();
		}

		bool NetClient::IsDemoRecording() const {
			return demoRecorder && demoRecorder->IsRecording();
		}

		float NetClient::GetDemoRecordingTime() const {
			return demoRecorder ? demoRecorder->GetRecordingTime() : 0.0F;
		}

		uint64_t NetClient::GetDemoPacketCount() const {
			return demoRecorder ? demoRecorder->GetPacketCount() : 0;
		}

		const std::string& NetClient::GetDemoFilename() const {
			static std::string empty;
			return demoRecorder ? demoRecorder->GetFilename() : empty;
		}
	} // namespace client
} // namespace spades