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
#include "GameMap.h"
#include "GameMapLoader.h"
#include "GameProperties.h"
#include "Grenade.h"
#include "NetClient.h"
#include "Player.h"
#include "ProtocolCodec.h"
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

DEFINE_SPADES_SETTING(cg_defaultBlockColorR, "111");
DEFINE_SPADES_SETTING(cg_defaultBlockColorG, "111");
DEFINE_SPADES_SETTING(cg_defaultBlockColorB, "111");

// cg_unicode is DEFINEd in ProtocolCodec.cpp (it moved with the string helpers, D-04);
// reference it here so SendVersionEnhanced can read the SupportsUnicode feature flag.
SPADES_SETTING(cg_unicode);

namespace spades {
	namespace client {

			namespace {
				enum { BLUE_FLAG = 0, GREEN_FLAG = 1, BLUE_BASE = 2, GREEN_BASE = 3 };

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
			} // namespace

			// NetPacketReader/NetPacketWriter, the PacketType enum, the cg_unicode setting,
			// and the EncodeString/DecodeString/ParsePlayerInput/ParseWeaponInput helpers now
			// live in ProtocolCodec.{h,cpp} (D-04). NetClient delegates to them via the codec's
			// Decode<Name>/Encode<Name> functions while keeping all stateful orchestration here.

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

						auto mapSize = DecodeMapStart(reader).mapSize;
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
				case PacketTypeHandShakeInit:
					SendHandShakeValid(DecodeHandShakeInit(r).challenge);
					return true;
				case PacketTypeExtensionInfo: HandleExtensionPacket(r); return true;
				case PacketTypeVersionGet: {
					auto s = DecodeVersionGet(r);
					if (!s.propertyIds.empty()) {
						// Enhanced variant
						std::set<std::uint8_t> propertyIds(s.propertyIds.begin(),
														   s.propertyIds.end());
						SendVersionEnhanced(propertyIds);
					} else {
						// Simple variant
						SendVersion();
					}
				}
					return true;
				default: return false;
			}
		}

		void NetClient::HandleExtensionPacket(spades::client::NetPacketReader& r) {
			auto s = DecodeExtensionInfo(r);
			for (const auto& e : s.extensions) {
				int extId = e.id;
				int extVer = e.version;

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
					p.RepositionPlayer(DecodePositionData(r).position);
				} break;
				case PacketTypeOrientationData: {
					Player& p = GetLocalPlayer();

					// ignore invalid orientation
					Vector3 o = DecodeOrientationData(r).orientation;
					if (o.GetSquaredLength() < 0.01F)
						break;

					o = o.Normalize();
					p.SetOrientation(o);
				} break;
				case PacketTypeWorldUpdate: {
					client->MarkWorldUpdate();

					// Decode the wire entries first (codec is pure, version-param-driven, D-08),
					// then apply the stateful mutation here (idx range-check, repositioning,
					// savedPlayer* writes) — the "decode-all-first, then mutate" safe split (D-09).
					auto s = DecodeWorldUpdate(r, protocolVersion);
					for (const auto& e : s.entries) {
						int idx = e.index;
						if (protocolVersion == 4) {
							if (idx < 0 || idx >= properties->GetMaxNumPlayerSlots())
								SPRaise("Invalid player ID %d received with WorldUpdate", idx);
						}

						Vector3 pos = e.position;
						Vector3 front = e.front;

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
						auto s = DecodeInputData(r);
						Player& p = GetPlayer(s.playerId);
						PlayerInput inp = ParsePlayerInput(s.bits);

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
						auto s = DecodeWeaponInput(r);
						Player& p = GetPlayer(s.playerId);
						WeaponInput inp = ParseWeaponInput(s.bits);

						if (&p == GetWorld()->GetLocalPlayer())
							break;

						p.SetWeaponInput(inp);
					}
					break;
				case PacketTypeSetHP: { // Hit Packet is Client-to-Server!
					Player& p = GetLocalPlayer();
					auto s = DecodeSetHP(r);
					int type = s.type; // 0=fall, 1=weap
					p.SetHP(s.hp, type ? HurtTypeWeapon : HurtTypeFall, s.source);
				} break;
				case PacketTypeGrenadePacket:
					if (!GetWorld())
						break;
					{
						auto s = DecodeGrenade(r); // playerId is decoded but unused (was skipped)
						Grenade* g =
							new Grenade(*GetWorld(), s.position, s.velocity, s.fuse);
						GetWorld()->AddGrenade(std::unique_ptr<Grenade>{g});
					}
					break;
				case PacketTypeSetTool: {
					auto s = DecodeSetTool(r);
					Player& p = GetPlayer(s.playerId);
					int tool = s.tool;

					switch (tool) {
						case 0: p.SetTool(Player::ToolSpade); break;
						case 1: p.SetTool(Player::ToolBlock); break;
						case 2: p.SetTool(Player::ToolWeapon); break;
						case 3: p.SetTool(Player::ToolGrenade); break;
						default: SPRaise("Received invalid tool type: %d", tool);
					}
				} break;
				case PacketTypeSetColour: {
					auto s = DecodeSetColour(r);
					stmp::optional<Player&> p = GetPlayerOrNull(s.playerId);
					if (p)
						p->SetHeldBlockColor(s.color);
					else
						temporaryPlayerBlockColor = s.color;
				} break;
				case PacketTypeExistingPlayer:
					if (!GetWorld())
						break;
					{
						// Decode the wire fields (codec is pure); keep all the stateful
						// construction/validation here (D-09). name normalization stays
						// in NetClient (display normalization, not wire format).
						auto s = DecodeExistingPlayer(r);
						int pId = s.playerId;
						int team = s.team;
						int weapon = s.weapon;
						int tool = s.tool;
						int score = static_cast<int>(s.score);
						IntVector3 color = s.color; // block color
						std::string name = StripNewlines(TrimSpaces(s.name));

						WeaponType wType;
						switch (weapon) {
							case 0: wType = RIFLE_WEAPON; break;
							case 1: wType = SMG_WEAPON; break;
							case 2: wType = SHOTGUN_WEAPON; break;
							default: SPRaise("Received invalid weapon: %d", weapon);
						}

						auto p = stmp::make_unique<Player>(*GetWorld(), pId, wType, team);

						// set position
						p->SetPosition(savedPlayerPos[pId]);

						// set block color
						p->SetHeldBlockColor(color);

						// set tool
						switch (tool) {
							case 0: p->SetTool(Player::ToolSpade); break;
							case 1: p->SetTool(Player::ToolBlock); break;
							case 2: p->SetTool(Player::ToolWeapon); break;
							case 3: p->SetTool(Player::ToolGrenade); break;
							default: SPRaise("Received invalid tool type: %d", tool);
						}

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

					auto s = DecodeMoveObject(r);
					int type = s.type;
					int state = s.state;
					Vector3 pos = s.position;

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

					// Decode the raw wire fields (codec is pure); the pos.z-=2.4 spawn
					// adjustment + construction + block-color override stay here (D-09).
					auto s = DecodeCreatePlayer(r);
					int pId = s.playerId;
					int weapon = s.weapon;
					int team = s.team;
					Vector3 pos = s.position;
					std::string name = StripNewlines(TrimSpaces(s.name));

					if (pId < 0 || pId >= properties->GetMaxNumPlayerSlots()) {
						SPLog("Ignoring invalid player ID %d (pyspades bug?: %s)", pId, name.c_str());
						break;
					}

					WeaponType wType;
					switch (weapon) {
						case 0: wType = RIFLE_WEAPON; break;
						case 1: wType = SMG_WEAPON; break;
						case 2: wType = SHOTGUN_WEAPON; break;
						default: SPRaise("Received invalid weapon: %d", weapon);
					}

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
					auto s = DecodeBlockAction(r);
					stmp::optional<Player&> p = GetPlayerOrNull(s.playerId);
					int action = s.action;
					IntVector3 pos = s.position;

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
					auto s = DecodeBlockLine(r);
					stmp::optional<Player&> p = GetPlayerOrNull(s.playerId);

					IntVector3 pos1 = s.start;
					IntVector3 pos2 = s.end;

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
						// receives my player info. Decode the discriminated wire fields
						// (codec is pure, D-10); build the World teams + CTF/TC game-mode
						// object here (D-09).
						auto s = DecodeStateData(r);

						World::Team& t1 = GetWorld()->GetTeam(0);
						World::Team& t2 = GetWorld()->GetTeam(1);
						t1.color = s.teamColor[0];
						t2.color = s.teamColor[1];
						t1.name = s.teamName[0];
						t2.name = s.teamName[1];

						GetWorld()->SetFogColor(s.fogColor);
						GetWorld()->SetLocalPlayerIndex(s.playerId);

						if (s.mode == CTFGameMode::m_CTF) { // CTF
							auto ctf = stmp::make_unique<CTFGameMode>();

							CTFGameMode::Team& team1 = ctf->GetTeam(0);
							CTFGameMode::Team& team2 = ctf->GetTeam(1);

							team1.score = s.ctfTeam1Score;
							team2.score = s.ctfTeam2Score;
							ctf->SetCaptureLimit(s.ctfCaptureLimit);

							int intelFlags = s.ctfIntelFlags;
							team1.hasIntel = (intelFlags & 1) != 0;
							team2.hasIntel = (intelFlags & 2) != 0;

							if (team2.hasIntel)
								team2.carrierId = s.ctfTeam2CarrierId;
							else
								team1.flagPos = s.ctfTeam1FlagPos;

							if (team1.hasIntel)
								team1.carrierId = s.ctfTeam1CarrierId;
							else
								team2.flagPos = s.ctfTeam2FlagPos;

							team1.basePos = s.ctfTeam1BasePos;
							team2.basePos = s.ctfTeam2BasePos;

							GetWorld()->SetMode(std::move(ctf));
						} else { // TC
							auto tc = stmp::make_unique<TCGameMode>(*GetWorld());

							for (const auto& terr : s.tcTerritories) {
								TCGameMode::Territory t{*tc};
								t.pos = terr.pos;

								t.ownerTeamId = terr.state;
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
					auto s = DecodeKillAction(r);
					int victimId = s.victimId;
					int killerId = s.killerId;
					int kt = s.killType;
					int respawnTime = s.respawnTime;

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
					auto s = DecodeChatMessage(r);
					int playerId = s.playerId;
					int type = s.type;
					std::string msg = StripNewlines(TrimSpaces(s.message));

					if (type == ChatTypeSystem) {
						if (playerId == 255) {
							customKickReasonString = msg.substr(0, 90);
							return;
						}

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
					}
				} break;
				case PacketTypeMapStart: {
					// next map!
					if (protocolVersion == 4)
						SendMapCached();

					client->SetWorld(NULL);

					auto mapSize = DecodeMapStart(r).mapSize;
					SPLog("Map size advertised by the server: %lu", (unsigned long)mapSize);

					mapLoader.reset(new GameMapLoader());
					mapLoadMonitor.reset(new MapDownloadMonitor(*mapLoader));

					status = NetClientStatusReceivingMap;
					statusString = _Tr("NetClient", "Loading snapshot");
				} break;
				case PacketTypeMapChunk: SPRaise("Unexpected: received Map Chunk while game");
				case PacketTypePlayerLeft: {
					int pId = DecodePlayerLeft(r).playerId;
					Player& p = GetPlayer(pId);

					client->PlayerLeaving(p);
					GetWorld()->GetPlayerPersistent(pId).score = 0;

					savedPlayerTeam[pId] = -1;
					GetWorld()->SetPlayer(pId, NULL);
				} break;
				case PacketTypeTerritoryCapture: {
					auto s = DecodeTerritoryCapture(r);
					int territoryId = s.territoryId;
					bool winning = s.winning != 0;
					int state = s.state;

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
					auto s = DecodeProgressBar(r);
					int territoryId = s.territoryId;
					int capturingTeam = s.capturingTeam;
					int rate = s.rate;
					float progress = s.progress;

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

					auto s = DecodeIntelCapture(r);
					int pId = s.playerId;
					Player& p = GetPlayer(pId);
					int teamId = p.GetTeamId();

					auto& ctf = dynamic_cast<CTFGameMode&>(mode.value());
					CTFGameMode::Team& team = ctf.GetTeam(teamId);
					team.score++;
					team.hasIntel = false;

					client->PlayerCapturedIntel(p);
					GetWorld()->GetPlayerPersistent(pId).score += 10;

					bool winning = s.winning != 0;
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

					int pId = DecodeIntelPickup(r).playerId;
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

					auto s = DecodeIntelDrop(r);
					Player& p = GetPlayer(s.playerId);
					int teamId = p.GetTeamId();

					auto& ctf = dynamic_cast<CTFGameMode&>(mode.value());
					ctf.GetTeam(teamId).hasIntel = false;
					ctf.GetTeam(1 - teamId).flagPos = s.position;
					client->PlayerDropIntel(p);
				} break;
				case PacketTypeRestock: {
					DecodeRestock(r); // playerId decoded but unused (was skipped)
					Player& p = GetLocalPlayer();
					p.Restock();
				} break;
				case PacketTypeFogColour: {
					if (GetWorld()) {
						// alpha is decoded but skipped (matches recv); color is BGR.
						GetWorld()->SetFogColor(DecodeFogColour(r).color);
					}
				} break;
				case PacketTypeWeaponReload: {
					// KEPT INLINE (deviation): recv reads clip/reserve ONLY for the local
					// player (conditional on direction), whereas DecodeWeaponReload reads all
					// 3 bytes unconditionally. Delegating would read 2 bytes the recv path does
					// not consume for non-local players — a wire-read change. The codec models
					// the full 3-byte send shape (round-trip tested); the recv stays inline to
					// preserve byte-identical reads (D-11 safety).
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
					DecodeChangeTeam(r); // playerId/team decoded but unused (were skipped)

					/*
						Player& p = GetPlayer(pId);
						if (team < 0 || team > 2)
							SPRaise("Received invalid team: %d", team);
						p.SetTeam(team);
					*/
				} break;
				case PacketTypeChangeWeapon: {
					DecodeChangeWeapon(r); // playerId/weapon decoded but unused (were skipped)

					/*
						Player& p = GetPlayer(pId);
						WeaponType wType;
						switch (weapon) {
							case 0: wType = RIFLE_WEAPON; break;
							case 1: wType = SMG_WEAPON; break;
							case 2: wType = SHOTGUN_WEAPON; break;
							default: SPRaise("Received invalid weapon: %d", weapon);
						}
						p.SetWeaponType(wType);
					*/
				} break;
				case PacketTypePlayerProperties: {
					auto s = DecodePlayerProperties(r); // subId decoded but unused (was skipped)
					int pId = s.playerId;
					int hp = s.hp;
					int blocks = s.blocks;
					int grenades = s.grenades;
					int clip = s.clip;
					int reserve = s.reserve;
					int score = s.score;

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

			int weapId;
			switch (weapType) {
				case RIFLE_WEAPON: weapId = 0; break;
				case SMG_WEAPON: weapId = 1; break;
				case SHOTGUN_WEAPON: weapId = 2; break;
				default: SPInvalidEnum("weapType", weapType);
			}

			NetPacketWriter w(PacketTypeExistingPlayer);
			w.WriteByte((uint8_t)0); // Player ID, but shouldn't matter here
			w.WriteByte((uint8_t)team);
			w.WriteByte((uint8_t)weapId);
			w.WriteByte((uint8_t)2); // TODO: change tool
			w.WriteInt((uint32_t)score);
			w.WriteColor(GetWorld()->GetTeamColor(team));
			w.WriteString(name, 16);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendPosition(spades::Vector3 v) {
			SPADES_MARK_FUNCTION();

			PositionDataPacket s;
			s.position = v;
			auto w = EncodePositionData(s);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendOrientation(spades::Vector3 v) {
			SPADES_MARK_FUNCTION();

			OrientationDataPacket s;
			s.orientation = v;
			auto w = EncodeOrientationData(s);
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

				InputDataPacket s;
				s.playerId = (uint8_t)GetLocalPlayer().GetId();
				s.bits = bits;
				auto w = EncodeInputData(s);

				// Record to demo before sending (server doesn't echo this back)
				if (demoRecorder && demoRecorder->IsRecording()) {
					auto data = w.GetData();
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

				WeaponInputPacket s;
				s.playerId = (uint8_t)GetLocalPlayer().GetId();
				s.bits = bits;
				auto w = EncodeWeaponInput(s);

				// Record to demo before sending (server doesn't echo this back)
				if (demoRecorder && demoRecorder->IsRecording()) {
					auto data = w.GetData();
					demoRecorder->RecordPacket(data.data(), data.size());
				}

				enet_peer_send(peer, 0, w.CreatePacket());
			}

		void NetClient::SendHit(int targetPlayerId, HitType type) {
			SPADES_MARK_FUNCTION();

			HitPacketPacket s;
			s.targetId = (uint8_t)targetPlayerId;
			switch (type) {
				case HitTypeTorso: s.hitType = 0; break;
				case HitTypeHead: s.hitType = 1; break;
				case HitTypeArms: s.hitType = 2; break;
				case HitTypeLegs: s.hitType = 3; break;
				case HitTypeMelee: s.hitType = 4; break;
				default: SPInvalidEnum("type", type);
			}
			auto w = EncodeHitPacket(s);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

			void NetClient::SendGrenade(const Grenade& g) {
				SPADES_MARK_FUNCTION();

				GrenadePacket s;
				s.playerId = (uint8_t)GetLocalPlayer().GetId();
				s.fuse = g.GetFuse();
				s.position = g.GetPosition();
				s.velocity = g.GetVelocity();
				auto w = EncodeGrenade(s);

				// Record to demo before sending (server doesn't echo this back)
				if (demoRecorder && demoRecorder->IsRecording()) {
					auto data = w.GetData();
					demoRecorder->RecordPacket(data.data(), data.size());
				}

				enet_peer_send(peer, 0, w.CreatePacket());
			}

		void NetClient::SendTool() {
			SPADES_MARK_FUNCTION();

			SetToolPacket s;
			s.playerId = (uint8_t)GetLocalPlayer().GetId();
			Player::ToolType type = GetLocalPlayer().GetTool();
				switch (type) {
					case Player::ToolSpade: s.tool = 0; break;
					case Player::ToolBlock: s.tool = 1; break;
					case Player::ToolWeapon: s.tool = 2; break;
					case Player::ToolGrenade: s.tool = 3; break;
					default: SPInvalidEnum("tool", type);
				}
				auto w = EncodeSetTool(s);

				// Record to demo before sending (server doesn't echo this back)
				if (demoRecorder && demoRecorder->IsRecording()) {
					auto data = w.GetData();
					demoRecorder->RecordPacket(data.data(), data.size());
				}

				enet_peer_send(peer, 0, w.CreatePacket());
			}

			void NetClient::SendHeldBlockColor() {
				SPADES_MARK_FUNCTION();

				SetColourPacket s;
				s.playerId = (uint8_t)GetLocalPlayer().GetId();
				s.color = GetLocalPlayer().GetBlockColor();
				auto w = EncodeSetColour(s);

				// Record to demo before sending (server doesn't echo this back)
				if (demoRecorder && demoRecorder->IsRecording()) {
					auto data = w.GetData();
					demoRecorder->RecordPacket(data.data(), data.size());
				}

				enet_peer_send(peer, 0, w.CreatePacket());
			}

		void NetClient::SendBlockAction(spades::IntVector3 v, BlockActionType type) {
			SPADES_MARK_FUNCTION();

			BlockActionPacket s;
			s.playerId = (uint8_t)GetLocalPlayer().GetId();
			switch (type) {
				case BlockActionCreate: s.action = 0; break;
				case BlockActionTool: s.action = 1; break;
				case BlockActionDig: s.action = 2; break;
				case BlockActionGrenade: s.action = 3; break;
				default: SPInvalidEnum("type", type);
			}
			s.position = v;
			auto w = EncodeBlockAction(s);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendBlockLine(spades::IntVector3 v1, spades::IntVector3 v2) {
			SPADES_MARK_FUNCTION();

			BlockLinePacket s;
			s.playerId = (uint8_t)GetLocalPlayer().GetId();
			s.start = v1;
			s.end = v2;
			auto w = EncodeBlockLine(s);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendChat(std::string text, bool global) {
			SPADES_MARK_FUNCTION();

			ChatMessagePacket s;
			s.playerId = (uint8_t)GetLocalPlayer().GetId();
			s.type = (uint8_t)(global ? 0 : 1);
			s.message = text;
			auto w = EncodeChatMessage(s); // appends the trailing NUL (matches recv framing)
			enet_peer_send(peer, 0, w.CreatePacket());
		}

			void NetClient::SendReload() {
				SPADES_MARK_FUNCTION();

				WeaponReloadPacket s;
				s.playerId = (uint8_t)GetLocalPlayer().GetId();
				s.clip = 0;	   // clip_ammo; not used?
				s.reserve = 0; // reserve_ammo; not used?
				auto w = EncodeWeaponReload(s);

				// Record to demo before sending (server response is delayed)
				if (demoRecorder && demoRecorder->IsRecording()) {
					auto data = w.GetData();
					demoRecorder->RecordPacket(data.data(), data.size());
				}

				enet_peer_send(peer, 0, w.CreatePacket());
			}

		void NetClient::SendTeamChange(int team) {
			SPADES_MARK_FUNCTION();

			ChangeTeamPacket s;
			s.playerId = (uint8_t)GetLocalPlayer().GetId();
			s.team = (uint8_t)team;
			auto w = EncodeChangeTeam(s);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendWeaponChange(WeaponType wType) {
			SPADES_MARK_FUNCTION();

			ChangeWeaponPacket s;
			s.playerId = (uint8_t)GetLocalPlayer().GetId();
			s.weapon = (uint8_t)wType;
			auto w = EncodeChangeWeapon(s);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendMapCached() {
			SPADES_MARK_FUNCTION();

			// The AoS 0.76 protocol allows the client to load a map from a local cache
			// if possible. After receiving MapStart, the client should respond with
			// MapCached to indicate whether the map with a given checksum exists in the
			// cache or not. We didn't implement a local cache, so we always ask the
			// server to send fresh map data.
			MapCachedPacket s;
			s.cached = 0;
			auto w = EncodeMapCached(s);
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendHandShakeValid(int challenge) {
			SPADES_MARK_FUNCTION();

			HandShakeReturnPacket s;
			s.challenge = (uint32_t)challenge;
			auto w = EncodeHandShakeReturn(s);

			SPLog("Sending hand shake back.");
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendVersion() {
			SPADES_MARK_FUNCTION();

			std::string osInfo = VersionInfo::GetVersionInfo();
			std::string archInfo = VersionInfo::GetAppArchitecture();

			osInfo.append(" | " ZEROSPADES_VER_STR);
			osInfo.append(" (" + archInfo + ")");

			VersionSendPacket s;
			s.tag = (uint8_t)'o';
			s.major = (uint8_t)OPENSPADES_VERSION_MAJOR;
			s.minor = (uint8_t)OPENSPADES_VERSION_MINOR;
			s.patch = (uint8_t)OPENSPADES_VERSION_PATCH;
			s.osInfo = osInfo;
			auto w = EncodeVersionSend(s);

			SPLog("Sending version back.");
			enet_peer_send(peer, 0, w.CreatePacket());
		}

		void NetClient::SendSupportedExtensions() {
			SPADES_MARK_FUNCTION();

			ExtensionInfoPacket s;
			for (const auto& i : extensions) {
				ExtensionInfoEntry e;
				e.id = static_cast<uint8_t>(i.first);		// ext id
				e.version = static_cast<uint8_t>(i.second); // ext version
				s.extensions.push_back(e);
			}
			auto w = EncodeExtensionInfo(s);

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

			// Step 1: Compress and write map data
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

			// Step 2: Write StateData packet
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

			// Step 3: Write ExistingPlayer packets for all players
			for (unsigned int i = 0; i < world->GetNumPlayerSlots(); i++) {
				stmp::optional<Player&> maybePlayer = world->GetPlayer(i);
				if (!maybePlayer)
					continue;

				Player& p = *maybePlayer;

				NetPacketWriter w(PacketTypeExistingPlayer);
				w.WriteByte(static_cast<uint8_t>(i));  // Player ID
				w.WriteByte(static_cast<uint8_t>(p.GetTeamId()));  // Team
				w.WriteByte(static_cast<uint8_t>(p.GetWeaponType()));  // Weapon

				// Tool
				int tool = 0;
				switch (p.GetTool()) {
					case Player::ToolSpade: tool = 0; break;
					case Player::ToolBlock: tool = 1; break;
					case Player::ToolWeapon: tool = 2; break;
					case Player::ToolGrenade: tool = 3; break;
				}
				w.WriteByte(static_cast<uint8_t>(tool));

				// Kill count (score)
				w.WriteInt(static_cast<uint32_t>(world->GetPlayerPersistent(i).score));

				// Block color
				w.WriteColor(p.GetBlockColor());

				// Name
				w.WriteString(world->GetPlayerPersistent(i).name);

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
			return demoRecorder ? demoRecorder->GetRecordingTime() : 0.0f;
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
