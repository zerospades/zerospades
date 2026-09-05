/*
 Copyright (c) 2026 Francois ND
 based on code of OpenSpades (c) yvt 2013.
 based on code of pysnip (c) Mathias Kaerlev 2011-2012.

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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <cstring>

#include "CTFGameMode.h"
#include "Client.h"
#include "DemoNetClient.h"
#include "ExtendedTeamplay.h"
#include "NetProtocol.h"
#include "GameMap.h"
#include "GameMapLoader.h"
#include "GameProperties.h"
#include "Grenade.h"
#include "Player.h"
#include "TCGameMode.h"
#include "Weapon.h"
#include "World.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Core/TMPUtils.h>
#include "IWorldListener.h"

SPADES_SETTING(cg_keyDemoPlayPause);

namespace spades {
	namespace client {

		namespace {
			// World listener used during fast replay: forwards only PlayerObjectSet (which
			// populates clientPlayers) and silently drops all sound/effect callbacks.
			class SilentWorldListener : public IWorldListener {
				Client* client;
			public:
				SilentWorldListener(Client* c) : client(c) {}
				void PlayerObjectSet(int id) override { client->PlayerObjectSet(id); }
				void PlayerMadeFootstep(Player&) override {}
				void PlayerJumped(Player&) override {}
				void PlayerLanded(Player&, bool) override {}
				void PlayerFiredWeapon(Player&) override {}
				void PlayerEjectedBrass(Player&) override {}
				void PlayerDryFiredWeapon(Player&) override {}
				void PlayerReloadingWeapon(Player&) override {}
				void PlayerReloadedWeapon(Player&) override {}
				void PlayerChangedTool(Player&) override {}
				void PlayerPulledGrenadePin(Player&) override {}
				void PlayerThrewGrenade(Player&, stmp::optional<const Grenade&>) override {}
				void PlayerMissedSpade(Player&) override {}
				void PlayerRestocked(Player&) override {}
				void PlayerHitBlockWithSpade(Player&, Vector3, IntVector3, IntVector3) override {}
				void PlayerKilledPlayer(Player&, Player&, KillType) override {}
				void BulletHitPlayer(Player&, HitType, Vector3, Player&,
									 std::unique_ptr<IBulletHitScanState>&) override {}
				void BulletNearPlayer(Player&) override {}
				void BulletHitBlock(Vector3, IntVector3, IntVector3) override {}
				void AddBulletTracer(Player&, Vector3, Vector3) override {}
				void GrenadeExploded(const Grenade&) override {}
				void GrenadeBounced(const Grenade&) override {}
				void GrenadeDroppedIntoWater(const Grenade&) override {}
				void BlocksFell(std::vector<IntVector3>) override {}
				void LocalPlayerBlockAction(IntVector3, BlockActionType) override {}
				void LocalPlayerCreatedLineBlock(IntVector3, IntVector3) override {}
				void LocalPlayerHurt(HurtType, Vector3) override {}
				void LocalPlayerBuildError(BuildFailureReason) override {}
			};
		} // anonymous namespace (SilentWorldListener)


		DemoNetClient::DemoNetClient(Client* c) : client(c), status(NetClientStatusNotConnected),
			expectedMapSize(0), receivedMapBytes(0),
			recordedLocalPlayerId(-1), seekingMode(false) {
			SPADES_MARK_FUNCTION();

			demoPlayer.reset(new DemoPlayer());

			const int slots = GameProperties::kMaxPlayerSlots;
			savedPlayerPos.resize(slots);
			savedPlayerFront.resize(slots);
			savedPlayerTeam.resize(slots);
			std::fill(savedPlayerTeam.begin(), savedPlayerTeam.end(), -1);

			statusString = _Tr("NetClient", "Not connected");
		}

		DemoNetClient::~DemoNetClient() {
			SPADES_MARK_FUNCTION();

			if (demoPlayer)
				demoPlayer->Close();
		}

		bool DemoNetClient::OpenDemo(const std::string& filename) {
			SPADES_MARK_FUNCTION();

			if (!demoPlayer->Open(filename)) {
				statusString = _Tr("NetClient", "Failed to open demo file");
				return false;
			}

			// Create game properties based on protocol version.
			// The demo viewer is always considered staff so admin-only
			// tools (free third-person, ESP boxes) are available for review.
			ProtocolVersion protoVer = (demoPlayer->GetProtocolVersion() == 4)
				? ProtocolVersion::v076 : ProtocolVersion::v075;
			properties.reset(new GameProperties(protoVer));
			properties->isStaff = true;

			status = NetClientStatusConnecting;
			statusString = _Tr("NetClient", "Loading demo");

			SPLog("Opened demo: %s (protocol %d)", filename.c_str(), demoPlayer->GetProtocolVersion());
			return true;
		}

		void DemoNetClient::DoEvents(float dt) {
			SPADES_MARK_FUNCTION();

			if (status == NetClientStatusNotConnected)
				return;

			// Process packets from the demo
			demoPlayer->Update(dt, [this](const std::vector<char>& data) {
				ProcessPacket(data);
			});

			// Check if demo finished - auto-pause when complete
			if (demoPlayer->IsFinished() && status == NetClientStatusConnected && !demoPlayer->IsPaused()) {
				SPLog("Demo playback finished");
				statusString = _Tr("NetClient", "Demo finished - press [{0}] to replay", ToUpperCase(cg_keyDemoPlayPause));
				demoPlayer->Pause();
			}
		}

		void DemoNetClient::ProcessPacket(const std::vector<char>& data) {
			SPADES_MARK_FUNCTION();

			if (data.empty())
				return;

			NetPacketReader reader(data);

			try {
				if (status == NetClientStatusConnecting) {
					int type = reader.GetType();
					if (type == PacketTypeMapStart) {
						auto mapSize = reader.ReadInt();
						expectedMapSize = mapSize;
						receivedMapBytes = 0;
						SPLog("Demo map size: %lu", (unsigned long)mapSize);

						mapLoader.reset(new GameMapLoader());
						status = NetClientStatusReceivingMap;
						statusString = _Tr("NetClient", "Loading map from demo");
					}
				} else if (status == NetClientStatusReceivingMap) {
					SPAssert(mapLoader);

					int type = reader.GetType();
					if (type == PacketTypeMapChunk) {
						const std::vector<char>& dt = reader.GetData();
						size_t chunkSize = dt.size() - 1;
						receivedMapBytes += chunkSize;
						mapLoader->AddRawChunk(dt.data() + 1, chunkSize);
					} else if (type == PacketTypeStateData) {
						status = NetClientStatusConnected;
						statusString = _Tr("NetClient", "Playing demo");

						try {
							MapLoaded();
						} catch (const std::exception& ex) {
							SPLog("Map loading error: %s", ex.what());
							status = NetClientStatusNotConnected;
							statusString = _Tr("NetClient", "Error loading map");
							throw;
						}

						HandleGamePacket(reader);
					} else {
						// Save packet for later processing
						preMapPackets.push_back(reader.GetData());
					}
				} else if (status == NetClientStatusConnected) {
					HandleGamePacket(reader);
				}
			} catch (const std::exception& ex) {
				int type = reader.GetType();
				reader.DumpDebug();
				SPLog("Exception handling demo packet 0x%02x: %s", type, ex.what());
			}
		}

		stmp::optional<World&> DemoNetClient::GetWorld() { return client->GetWorld(); }

		stmp::optional<Player&> DemoNetClient::GetPlayerOrNull(int pId) {
			SPADES_MARK_FUNCTION();
			if (!GetWorld())
				return {};
			if (pId < 0 || pId >= static_cast<int>(GetWorld()->GetNumPlayerSlots()))
				return {};
			return GetWorld()->GetPlayer(pId);
		}

		Player& DemoNetClient::GetPlayer(int pId) {
			SPADES_MARK_FUNCTION();
			if (!GetWorld())
				SPRaise("Invalid player ID %d: no world", pId);
			if (pId < 0 || pId >= static_cast<int>(GetWorld()->GetNumPlayerSlots()))
				SPRaise("Invalid player ID %d: out of range", pId);
			if (!GetWorld()->GetPlayer(pId))
				SPRaise("Invalid player ID %d: doesn't exist", pId);
			return GetWorld()->GetPlayer(pId).value();
		}

		stmp::optional<Player&> DemoNetClient::GetLocalPlayerOrNull() {
			SPADES_MARK_FUNCTION();
			if (!GetWorld())
				return {};
			return GetWorld()->GetLocalPlayer();
		}

		Player& DemoNetClient::GetLocalPlayer() {
			SPADES_MARK_FUNCTION();
			stmp::optional<Player&> maybePlayer = GetLocalPlayerOrNull();
			if (!maybePlayer)
				SPRaise("Failed to get local player: doesn't exist");
			return maybePlayer.value();
		}

		void DemoNetClient::HandleGamePacket(NetPacketReader& r) {
			SPADES_MARK_FUNCTION();

			int packetType = r.GetType();

			switch (packetType) {
				case PacketTypePositionData: {
					if (r.GetLength() != 13) break;
					if (recordedLocalPlayerId < 0) break;
					auto p = GetPlayerOrNull(recordedLocalPlayerId);
					if (!p) break;
					p->RepositionPlayer(r.ReadVector3());
				} break;
				case PacketTypeOrientationData: {
					if (recordedLocalPlayerId < 0) break;
					auto p = GetPlayerOrNull(recordedLocalPlayerId);
					if (!p) break;
					Vector3 o = r.ReadVector3();
					if (o.GetSquaredLength() < 0.01F) break;
					o = o.Normalize();
					p->SetOrientation(o);
				} break;
				case PacketTypeWorldUpdate: {
					int bytesPerEntry = 24;
					if (demoPlayer->GetProtocolVersion() == 4)
						bytesPerEntry++;

					if (!seekingMode)
						client->MarkWorldUpdate();

					int entries = static_cast<int>(r.GetLength() / bytesPerEntry);
					for (int i = 0; i < entries; i++) {
						int idx = i;
						if (demoPlayer->GetProtocolVersion() == 4) {
							idx = r.ReadByte();
							if (idx < 0 || idx >= properties->GetMaxNumPlayerSlots())
								SPRaise("Invalid player ID %d in WorldUpdate", idx);
						}

						Vector3 pos = r.ReadVector3();
						Vector3 front = r.ReadVector3();

						if (GetWorld()) {
							auto p = GetWorld()->GetPlayer(idx);
							auto localPlayer = GetLocalPlayerOrNull();
							if (p && (!localPlayer || &*p != &*localPlayer)
								&& p->IsAlive() && !p->IsSpectator()) {
								p->RepositionPlayer(pos);
								p->SetOrientation(front);
							}
						}

						savedPlayerPos.at(idx) = pos;
						savedPlayerFront.at(idx) = front;
					}
				} break;
				case PacketTypeInputData:
					if (!GetWorld()) break;
					{
						auto p = GetPlayerOrNull(r.ReadByte());
						PlayerInput inp = ParsePlayerInput(r.ReadByte());
						if (!p) break;
						// In demo mode there's no local player (spectating), so check before comparing
						auto localPlayer = GetLocalPlayerOrNull();
						if (localPlayer && &*p == &*localPlayer) {
							if (inp.jump) p->PlayerJump();
							break;
						}
						p->SetInput(inp);
					}
					break;
				case PacketTypeWeaponInput:
					if (!GetWorld()) break;
					{
						auto p = GetPlayerOrNull(r.ReadByte());
						WeaponInput inp = ParseWeaponInput(r.ReadByte());
						if (!p) break;
						// In demo mode there's no local player (spectating), so check before comparing
						auto localPlayer = GetLocalPlayerOrNull();
						if (localPlayer && &*p == &*localPlayer) break;
						p->SetWeaponInput(inp);
					}
					break;
				case PacketTypeSetHP: {
					if (recordedLocalPlayerId < 0) break;
					auto p = GetPlayerOrNull(recordedLocalPlayerId);
					if (!p) break;
					int hp = r.ReadByte();
					int type = r.ReadByte();
					Vector3 source = r.ReadVector3();
					p->SetHP(hp, type ? HurtTypeWeapon : HurtTypeFall, source);
				} break;
				case PacketTypeGrenadePacket:
					if (!GetWorld()) break;
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
					auto p = GetPlayerOrNull(r.ReadByte());
					int tool = r.ReadByte();
					if (!p) break;
					switch (tool) {
						case 0: p->SetTool(Player::ToolSpade); break;
						case 1: p->SetTool(Player::ToolBlock); break;
						case 2: p->SetTool(Player::ToolWeapon); break;
						case 3: p->SetTool(Player::ToolGrenade); break;
						default: SPRaise("Invalid tool type: %d", tool);
					}
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
					if (!GetWorld()) break;
					{
						int pId = r.ReadByte();
						if (pId < 0 || pId >= properties->GetMaxNumPlayerSlots()) {
							SPLog("Ignoring invalid player ID %d in ExistingPlayer", pId);
							break;
						}

						int team = r.ReadByte();
						int weapon = r.ReadByte();
						int tool = r.ReadByte();
						int score = r.ReadInt();
						IntVector3 color = r.ReadIntColor();
						std::string name = TrimSpaces(r.ReadRemainingString());

						WeaponType wType;
						switch (weapon) {
							case 0: wType = RIFLE_WEAPON; break;
							case 1: wType = SMG_WEAPON; break;
							case 2: wType = SHOTGUN_WEAPON; break;
							default: SPRaise("Invalid weapon: %d", weapon);
						}

						auto p = stmp::make_unique<Player>(*GetWorld(), pId, wType, team);
						p->SetPosition(savedPlayerPos.at(pId));
						p->SetHeldBlockColor(color);

						switch (tool) {
							case 0: p->SetTool(Player::ToolSpade); break;
							case 1: p->SetTool(Player::ToolBlock); break;
							case 2: p->SetTool(Player::ToolWeapon); break;
							case 3: p->SetTool(Player::ToolGrenade); break;
							default: SPRaise("Invalid tool type: %d", tool);
						}

						GetWorld()->SetPlayer(pId, std::move(p));

						auto& pers = GetWorld()->GetPlayerPersistent(pId);
						pers.name = name;
						pers.score = score;

						savedPlayerTeam.at(pId) = team;
					}
					break;
				case PacketTypeMoveObject: {
					if (!GetWorld()) SPRaise("No world");

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
						if (type < tc.GetNumTerritories()) {
							TCGameMode::Territory& t = tc.GetTerritory(type);
							t.pos = pos;
							t.ownerTeamId = state;
						}
					}
				} break;
				case PacketTypeCreatePlayer: {
					if (!GetWorld()) SPRaise("No world");

					int pId = r.ReadByte();
					int weapon = r.ReadByte();
					int team = r.ReadByte();
					Vector3 pos = r.ReadVector3();
					std::string name = TrimSpaces(r.ReadRemainingString());

					if (pId < 0 || pId >= properties->GetMaxNumPlayerSlots()) {
						SPLog("Ignoring invalid player ID %d", pId);
						break;
					}

					WeaponType wType;
					switch (weapon) {
						case 0: wType = RIFLE_WEAPON; break;
						case 1: wType = SMG_WEAPON; break;
						case 2: wType = SHOTGUN_WEAPON; break;
						default: SPRaise("Invalid weapon: %d", weapon);
					}

					auto p = stmp::make_unique<Player>(*GetWorld(), pId, wType, team);
					pos.z -= 2.4F;
					p->SetPosition(pos);

					GetWorld()->SetPlayer(pId, std::move(p));

					if (!name.empty())
						GetWorld()->GetPlayerPersistent(pId).name = name;

					Player& pRef = GetWorld()->GetPlayer(pId).value();

					// In demo mode, user is always spectator - track all team changes
					if (savedPlayerTeam.at(pId) != team) {
						if (!seekingMode)
							client->PlayerJoinedTeam(pRef);
						savedPlayerTeam.at(pId) = team;
					}

					if (!seekingMode)
						client->PlayerSpawned(pRef);
					else
						// A seek replays the marks to rebuild what was in force at the
						// destination, so it has to replay what ends them too.
						client->ExtendedTeamplayPlayerSpawned(pId);
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
								if (p->IsLocalPlayer() && !seekingMode)
									client->RegisterPlacedBlocks(1);
							}
							if (!seekingMode)
								client->PlayerCreatedBlock(*p);
						}
					} else if (action == BlockActionTool) {
						cells.push_back(pos);
						GetWorld()->DestroyBlock(cells);
						if (p && p->IsToolSpade())
							p->GotBlock();
						if (!seekingMode)
							client->PlayerDestroyedBlockWithWeaponOrTool(pos);
					} else if (action == BlockActionDig) {
						for (int z = -1; z <= 1; z++)
							cells.push_back(MakeIntVector3(pos.x, pos.y, pos.z + z));
						GetWorld()->DestroyBlock(cells);
						if (!seekingMode)
							client->PlayerDiggedBlock(pos);
					} else if (action == BlockActionGrenade) {
						for (int x = -1; x <= 1; x++)
						for (int y = -1; y <= 1; y++)
						for (int z = -1; z <= 1; z++)
							cells.push_back(MakeIntVector3(pos.x + x, pos.y + y, pos.z + z));
						GetWorld()->DestroyBlock(cells);
						if (!seekingMode)
							client->GrenadeDestroyedBlock(pos);
					}
				} break;
				case PacketTypeBlockLine: {
					stmp::optional<Player&> p = GetPlayerOrNull(r.ReadByte());
					IntVector3 pos1 = r.ReadIntVector3();
					IntVector3 pos2 = r.ReadIntVector3();

					auto cells = GetWorld()->CubeLine(pos1, pos2, 50);

					for (const auto& c : cells) {
						if (!GetWorld()->GetMap()->IsSolid(c.x, c.y, c.z))
							GetWorld()->CreateBlock(c, p ? p->GetBlockColor() : temporaryPlayerBlockColor);
					}

					if (p) {
						int blocks = static_cast<int>(cells.size());
						p->UseBlocks(blocks);
						if (p->IsLocalPlayer() && !seekingMode)
							client->RegisterPlacedBlocks(blocks);
						if (!seekingMode)
							client->PlayerCreatedBlock(*p);
					}
				} break;
				case PacketTypeStateData:
					if (!GetWorld()) break;
					{
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
						// In demo mode, don't set local player - we're spectating
						// Store the recorded player ID for potential follow-cam
						recordedLocalPlayerId = pId;

						int mode = r.ReadByte();
						if (mode == CTFGameMode::m_CTF) {
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
						} else {
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
						if (!seekingMode)
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

					auto victim = GetPlayerOrNull(victimId);
					auto killer = GetPlayerOrNull(killerId);
					if (!victim || !killer) {
						SPLog("Demo: KillAction skipped - player not found (victim=%d, killer=%d)", victimId, killerId);
						break;
					}
					victim->KilledBy(type, *killer, respawnTime);
					if (killerId != victimId)
						GetWorld()->GetPlayerPersistent(killerId).score++;
				} break;
				case PacketTypeChatMessage: {
					int playerId = r.ReadByte();
					int type = r.ReadByte();
					std::string msg = TrimSpaces(r.ReadRemainingString());

					if (!seekingMode) {
						if (type == 2) { // System
							client->ServerSentMessage(false, msg);
							properties->HandleServerMessage(msg);
						} else if (type == 0 || type == 1) { // All or Team
							stmp::optional<Player&> p = GetPlayerOrNull(playerId);
							if (p) {
								client->PlayerSentChatMessage(*p, (type == 0), msg);
							} else {
								client->ServerSentMessage((type == 1), msg);
							}
						} else if (type == ChatTypeDirect) {
							// Recorded from the ExtendedTeamplay extension: the line was
							// sent to the recording player alone, by the player this
							// packet names or by the server itself.
							stmp::optional<Player&> p = GetPlayerOrNull(playerId);
							client->ServerSentDirectMessage(p ? (*p).GetName() : std::string(),
															msg);
						}
					}
				} break;
				case PacketTypePlayerLeft: {
					int pId = r.ReadByte();
					auto p = GetPlayerOrNull(pId);
					if (p && !seekingMode)
						client->PlayerLeaving(*p);
					GetWorld()->GetPlayerPersistent(pId).score = 0;
					if (pId >= 0 && pId < (int)savedPlayerTeam.size())
						savedPlayerTeam[pId] = -1;
					GetWorld()->SetPlayer(pId, NULL);
				} break;
				case PacketTypeTerritoryCapture: {
					int territoryId = r.ReadByte();
					bool winning = r.ReadByte() != 0;
					int state = r.ReadByte();

					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode || mode->ModeType() != IGameMode::m_TC) break;

					auto& tc = dynamic_cast<TCGameMode&>(*mode);
					if (territoryId >= tc.GetNumTerritories()) break;

					if (!seekingMode)
						client->TeamCapturedTerritory(state, territoryId);

					TCGameMode::Territory& t = tc.GetTerritory(territoryId);
					t.ownerTeamId = state;
					t.progressBasePos = 0.0F;
					t.progressRate = 0.0F;
					t.progressStartTime = 0.0F;
					t.capturingTeamId = -1;

					if (winning && !seekingMode)
						client->TeamWon(state);
				} break;
				case PacketTypeProgressBar: {
					int territoryId = r.ReadByte();
					int capturingTeam = r.ReadByte();
					int rate = (int8_t)r.ReadByte();
					float progress = r.ReadFloat();

					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode || mode->ModeType() != IGameMode::m_TC) break;

					auto& tc = dynamic_cast<TCGameMode&>(*mode);
					if (territoryId >= tc.GetNumTerritories()) break;

					TCGameMode::Territory& t = tc.GetTerritory(territoryId);
					t.progressBasePos = progress;
					t.progressRate = (float)rate * TC_CAPTURE_RATE;
					t.progressStartTime = GetWorld()->GetTime();
					t.capturingTeamId = capturingTeam;
				} break;
				case PacketTypeIntelCapture: {
					if (!GetWorld()) break;

					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode || mode->ModeType() != IGameMode::m_CTF) break;

					int pId = r.ReadByte();
					auto p = GetPlayerOrNull(pId);
					if (!p) break;
					int teamId = p->GetTeamId();

					auto& ctf = dynamic_cast<CTFGameMode&>(mode.value());
					CTFGameMode::Team& team = ctf.GetTeam(teamId);
					team.score++;
					team.hasIntel = false;

					if (!seekingMode)
						client->PlayerCapturedIntel(*p);
					GetWorld()->GetPlayerPersistent(pId).score += 10;

					bool winning = r.ReadByte() != 0;
					if (winning) {
						if (!seekingMode)
							client->TeamWon(teamId);
						ctf.ResetIntelHoldingStatus();
					}
				} break;
				case PacketTypeIntelPickup: {
					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode || mode->ModeType() != IGameMode::m_CTF) break;

					int pId = r.ReadByte();
					auto p = GetPlayerOrNull(pId);
					if (!p) break;

					auto& ctf = dynamic_cast<CTFGameMode&>(mode.value());
					CTFGameMode::Team& team = ctf.GetTeam(p->GetTeamId());
					team.hasIntel = true;
					team.carrierId = pId;
					if (!seekingMode)
						client->PlayerPickedIntel(*p);
				} break;
				case PacketTypeIntelDrop: {
					stmp::optional<IGameMode&> mode = GetWorld()->GetMode();
					if (!mode || mode->ModeType() != IGameMode::m_CTF) break;

					int pId = r.ReadByte();
					auto p = GetPlayerOrNull(pId);
					if (!p) break;
					int teamId = p->GetTeamId();

					auto& ctf = dynamic_cast<CTFGameMode&>(mode.value());
					ctf.GetTeam(teamId).hasIntel = false;
					ctf.GetTeam(1 - teamId).flagPos = r.ReadVector3();
					if (!seekingMode)
						client->PlayerDropIntel(*p);
				} break;
				case PacketTypeRestock: {
					r.ReadByte(); // skip player id
					if (recordedLocalPlayerId >= 0) {
						auto p = GetPlayerOrNull(recordedLocalPlayerId);
						if (p) p->Restock();
					}
				} break;
				case PacketTypeFogColour: {
					if (GetWorld()) {
						r.ReadByte(); // skip alpha value
						GetWorld()->SetFogColor(r.ReadIntColor());
					}
				} break;
				case PacketTypeWeaponReload: {
					int pId = r.ReadByte();
					auto p = GetPlayerOrNull(pId);
					if (!p) break;
					int clip = r.ReadByte();
					int reserve = r.ReadByte();
					// For the recorded player, apply real ammo counts from external demos.
					// Our own demos record the client-sent packet (zeros), so non-zero
					// values reliably indicate a server-sent response.
					if (pId == recordedLocalPlayerId && (clip != 0 || reserve != 0)) {
						p->ReloadDone(clip, reserve);
					} else {
						p->Reload();
					}
				} break;
				case PacketTypeChangeTeam:
				case PacketTypeChangeWeapon:
					// These packets are ignored in demo playback
					break;
				case PacketTypePlayerProperties: {
					r.ReadByte(); // skip subId
					int pId = r.ReadByte();
					int hp = r.ReadByte();
					int blocks = r.ReadByte();
					int grenades = r.ReadByte();
					int clip = r.ReadByte();
					int reserve = r.ReadByte();
					int score = r.ReadByte();

					auto p = GetPlayerOrNull(pId);
					if (!p) break;
					Weapon& w = p->GetWeapon();

					// In demo mode, use recorded local player ID for restock
					if (pId == recordedLocalPlayerId)
						p->Restock(hp, grenades, blocks);
					w.Restock(clip, reserve);
					GetWorld()->GetPlayerPersistent(pId).score = score;
				} break;
				case PacketTypeExtendedTeamplay: {
					switch (r.ReadByte()) { // sub packet id
						case ExtendedTeamplaySubConfig:
							client->ExtendedTeamplayConfigured(r.ReadByte());
							break;
						case ExtendedTeamplaySubPing: {
							int pId = r.ReadByte();
							Vector3 pos = r.ReadVector3();
							float duration = r.ReadFloat();
							uint8_t surfaces = r.ReadByte();
							IntVector3 color = r.ReadIntColor();
							uint8_t messageId = r.ReadByte();
							std::string reason =
							  ExtendedTeamplay::SanitizeReason(r.ReadRemainingData());

							// The message id is reserved and ignored, so only a malformed
							// duration is a reason not to replay the ping.
							(void)messageId;
							if (!ExtendedTeamplay::IsValidDuration(duration))
								break;

							// A ping is a momentary event, so replaying the whole demo to
							// reach a seek target would otherwise pop every ping ever sent
							// at the destination. A removal is state and is kept.
							if (!seekingMode || duration == 0.0F)
								client->ExtendedTeamplayPingReceived(pId, pos, duration, surfaces,
																	 color, std::move(reason));
						} break;
						case ExtendedTeamplaySubESPMark: {
							int pId = r.ReadByte();
							float duration = r.ReadFloat();
							uint8_t surfaces = r.ReadByte();
							uint8_t flags = r.ReadByte();
							IntVector3 color = r.ReadIntColor();
							uint8_t messageId = r.ReadByte();
							std::string reason =
							  ExtendedTeamplay::SanitizeReason(r.ReadRemainingData());

							(void)messageId; // reserved and ignored, as on the ping
							if (!ExtendedTeamplay::IsValidDuration(duration))
								break;

							// Marks are state rather than events, so they are replayed
							// during a seek to rebuild what was in force. Their timers
							// restart from the seek, which a fast replay cannot avoid.
							client->ExtendedTeamplayMarkReceived(pId, duration, surfaces, flags,
																 color, std::move(reason));
						} break;
						default: break; // a sub packet from a newer extension version
					}
				} break;
				default:
					SPLog("Demo: dropped unknown packet %d", (int)r.GetType());
					break;
			}
		}

		void DemoNetClient::MapLoaded() {
			SPADES_MARK_FUNCTION();

			SPAssert(mapLoader);

			std::unique_ptr<GameMapLoader> loader = std::move(mapLoader);

			SPLog("Waiting for demo map decoding... (received %zu bytes)", receivedMapBytes);
			loader->MarkEOF();
			loader->WaitComplete();
			GameMap* map = loader->TakeGameMap().Unmanage();
			SPLog("Demo map decoded successfully.");

			World* w = new World(properties);
			w->SetMap(map);
			map->Release();
			SPLog("Demo world initialized.");

			client->SetWorld(w);

			SPAssert(GetWorld());

			SPLog("Processing %d saved demo packets...", (int)preMapPackets.size());

			std::fill(savedPlayerTeam.begin(), savedPlayerTeam.end(), -1);

			try {
				for (const auto& packets : preMapPackets) {
					NetPacketReader r(packets);
					HandleGamePacket(r);
				}
				preMapPackets.clear();
				SPLog("Demo packets processed.");
			} catch (...) {
				preMapPackets.clear();
				throw;
			}

			// Snapshot the map state at t=0 for backward seek support
			initialMap = GetWorld()->GetMap()->Clone();
			SPLog("Demo initial map snapshot saved.");
		}

		float DemoNetClient::GetMapReceivingProgress() {
			if (status != NetClientStatusReceivingMap || !mapLoader)
				return 0.0F;
			return mapLoader->GetProgress();
		}

		std::string DemoNetClient::GetStatusString() {
			if (status == NetClientStatusReceivingMap && mapLoader) {
				float progress = mapLoader->GetProgress();
				int per = (int)(progress * 100.0F);
				return _Tr("NetClient", "Loading map from demo ({0}%)", per);
			}
			return statusString;
		}

		void DemoNetClient::ResetWorldForReplay() {
			if (!initialMap) return;

			// Rebuild the world with a fresh copy of the initial map
			World* w = new World(properties);
			w->SetMap(initialMap->Clone());
			client->SetWorld(w);

			// Reset all per-player tracking state
			recordedLocalPlayerId = -1;
			std::fill(savedPlayerTeam.begin(), savedPlayerTeam.end(), -1);
			std::fill(savedPlayerPos.begin(), savedPlayerPos.end(), Vector3{});
			std::fill(savedPlayerFront.begin(), savedPlayerFront.end(), Vector3{});
		}

		void DemoNetClient::FastReplay(float targetTime) {
			seekingMode = true;

			// Replace the world listener with a silent stub so that kill sounds,
			// grenade effects, footsteps, etc. don't fire for every replayed packet.
			// PlayerObjectSet is still forwarded so clientPlayers[] stays in sync.
			SilentWorldListener silent(client);
			IWorldListener* savedListener = nullptr;
			if (GetWorld()) {
				savedListener = GetWorld()->GetListener();
				GetWorld()->SetListener(&silent);
			}

			try {
				demoPlayer->ReplayUpTo(targetTime,
					[this](const std::vector<char>& data, float dt) {
						ProcessPacket(data);
						// Age world physics in fixed steps so grenade fuses, falling
						// blocks, etc. resolve at their correct demo timestamps under
						// the silent listener instead of firing after replay finishes.
						if (auto w = GetWorld()) {
							const float step = 1.0F / 60.0F;
							while (dt >= step) {
								w->Advance(step);
								dt -= step;
							}
							if (dt > 0.0F)
								w->Advance(dt);
						}
					});
			} catch (...) {
				if (GetWorld())
					GetWorld()->SetListener(savedListener);
				seekingMode = false;
				throw;
			}

			if (GetWorld())
				GetWorld()->SetListener(savedListener);
			seekingMode = false;
		}

		void DemoNetClient::Seek(float time) {
			if (!demoPlayer) return;

			// FastReplay must always cover the bootstrap region so that the
			// rebuilt world has its game mode, teams and players (and so that
			// recordedLocalPlayerId is restored) — otherwise seeking back to
			// t=0 would leave the world empty and crash any subsequent draw
			// that dereferences the camera-target player.
			float replayTime = std::max(time, demoPlayer->GetBootstrapEndTime());

			if (initialMap) {
				auto view = client->SaveViewState();
				ResetWorldForReplay();
				FastReplay(replayTime);
				client->RestoreViewState(view);
			}

			demoPlayer->Seek(time);
			statusString = _Tr("NetClient", "Playing demo");
		}

		void DemoNetClient::SeekPreview(float time) {
			if (!demoPlayer) return;
			demoPlayer->Seek(time);
		}

		void DemoNetClient::SeekToBeginning() {
			if (!demoPlayer) return;

			if (initialMap && demoPlayer->GetTime() > 0.0F) {
				auto view = client->SaveViewState();
				ResetWorldForReplay();
				FastReplay(demoPlayer->GetBootstrapEndTime());
				client->RestoreViewState(view);
			}

			demoPlayer->Seek(0.0F);
			demoPlayer->Resume();
			statusString = _Tr("NetClient", "Playing demo");
		}

	} // namespace client
} // namespace spades
