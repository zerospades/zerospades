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

#include "EditorNetClient.h"

#include "Client.h"
#include "GameConstants.h"
#include "GameMap.h"
#include "GameProperties.h"
#include "Grenade.h"
#include "Player.h"
#include "TCGameMode.h"
#include "World.h"
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/Math.h>
#include <Core/TMPUtils.h>
#include <fstream>

namespace spades {
	namespace client {
		EditorNetClient::EditorNetClient(Client* c)
		    : client(c), status(NetClientStatusNotConnected), localPlayerId(0),
		      lastSpectatorPos(0, 0, 0), lastPlayerPos(0, 0, 0) {
			SPADES_MARK_FUNCTION();
		}

		EditorNetClient::~EditorNetClient() { SPADES_MARK_FUNCTION(); }

		bool EditorNetClient::LoadMap(const std::string& mapPath) {
			SPADES_MARK_FUNCTION();

			try {
				// For absolute file paths (editor files), use direct file I/O
				std::ifstream file(mapPath, std::ios::binary);
				if (!file.is_open()) {
					statusString = std::string("Failed to open map file: ") + mapPath;
					return false;
				}

				// Read entire file into memory
				file.seekg(0, std::ios::end);
				std::streamsize size = file.tellg();
				file.seekg(0, std::ios::beg);

				std::vector<char> buffer(size);
				if (!file.read(buffer.data(), size)) {
					statusString = "Failed to read map file";
					return false;
				}

				// Create a stream wrapper for the buffer
				class MemoryStream : public IStream {
					std::vector<char> data;
					size_t pos = 0;

				public:
					MemoryStream(std::vector<char> d) : data(std::move(d)) {}
					size_t Read(void* buf, size_t bytes) override {
						size_t toRead = std::min(bytes, data.size() - pos);
						if (toRead > 0) {
							std::memcpy(buf, data.data() + pos, toRead);
							pos += toRead;
						}
						return toRead;
					}
				};

				MemoryStream stream(std::move(buffer));
				map = Handle<GameMap>{GameMap::Load(&stream)};
				if (!map) {
					statusString = "Failed to load map data";
					return false;
				}

				status = NetClientStatusConnected;
				statusString = "Map loaded";

				// Initialize the world with the loaded map
				// The client will handle creating the world and player
				InitializeLocalPlayer();

				return true;
			} catch (const std::exception& ex) {
				statusString = std::string("Error loading map: ") + ex.what();
				return false;
			}
		}

		void EditorNetClient::Disconnect() {
			SPADES_MARK_FUNCTION();
			status = NetClientStatusNotConnected;
		}

		void EditorNetClient::HandleTeamSwitch(int teamId) {
			SPADES_MARK_FUNCTION();
			if (!client || !client->GetWorld())
				return;

			World* world = client->GetWorld();

			// Get the spectator camera position to spawn at
			Vector3 spawnPos = lastSpectatorPos;
			if (!spectatorPosSet) {
				// Fallback to map center if no spectator position set
				if (client->GetWorld()->GetMap()) {
					GameMap* map = client->GetWorld()->GetMap().GetPointerOrNull();
					spawnPos = Vector3(map->Width() / 2.0f, map->Height() / 2.0f, 32.0f);
				}
			}

			// Get or create player on the new team
			auto player = world->GetPlayer(localPlayerId);
			if (player) {
				Player& p = *player;
				p.SetTeam(teamId);
				p.SetHP(100, HurtTypeWeapon, MakeVector3(0, 0, 0));
				p.SetPosition(spawnPos);
				p.SetOrientation(MakeVector3(0.0f, 1.0f, 0.0f));
			}
		}

		void EditorNetClient::SendJoin(int team, WeaponType wType, std::string name, int score) {
			SPADES_MARK_FUNCTION();
			// Capture current spectator position before switching teams
			if (client) {
				// The client stores the free camera position - we'll access it via GetSpectatorPos
				// For now, use the lastSpectatorPos which should be set by Client
			}
			HandleTeamSwitch(team);
		}

		void EditorNetClient::SendTeamChange(int team) {
			SPADES_MARK_FUNCTION();
			HandleTeamSwitch(team);
		}

		void EditorNetClient::SendBlockAction(IntVector3 pos, BlockActionType type) {
			SPADES_MARK_FUNCTION();
			if (!client || !map) {
				SPLog("SendBlockAction: client=%p, map=%p", client, map.GetPointerOrNull());
				return;
			}

			World* world = client->GetWorld();
			if (!world) {
				SPLog("SendBlockAction: world is null");
				return;
			}

			// Clamp position to map bounds
			if (pos.x < 0 || pos.y < 0 || pos.z < 0)
				return;
			if (pos.x >= map->Width() || pos.y >= map->Height() || pos.z >= map->Depth())
				return;

			if (type == BlockActionCreate) {
				// Place block - use current player's held block color
				auto player = world->GetPlayer(localPlayerId);
				if (player) {
					world->CreateBlock(pos, player->GetBlockColor());
					client->PlayerCreatedBlock(*player);
				}
			} else if (type == BlockActionTool) {
				// Destroy single block with tool
				std::vector<IntVector3> cells;
				cells.push_back(pos);
				world->DestroyBlock(cells);
				client->PlayBlockDestroySound(MakeVector3(pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f));
			} else if (type == BlockActionDig) {
				// Destroy 3x3 area with spade
				std::vector<IntVector3> cells;
				for (int z = -1; z <= 1; z++)
					cells.push_back(MakeIntVector3(pos.x, pos.y, pos.z + z));
				world->DestroyBlock(cells);
				client->PlayBlockDestroySound(MakeVector3(pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f));
			}
			// Block actions are applied automatically in World::Advance()
		}

		void EditorNetClient::SendHit(int targetPlayerId, HitType type) {
			SPADES_MARK_FUNCTION();
			if (!client || !client->GetWorld())
				return;

			World* world = client->GetWorld();
			auto target = world->GetPlayer(targetPlayerId);

			if (!target)
				return;

			// Apply damage based on hit location
			int damage = 0;
			switch (type) {
				case HitTypeHead: damage = 100; break;
				case HitTypeTorso: damage = 50; break;
				case HitTypeArms: damage = 30; break;
				case HitTypeLegs: damage = 30; break;
				case HitTypeMelee: damage = 33; break;
				default: damage = 25; break;
			}

			int currentHP = target->GetHealth();
			int newHP = currentHP - damage;
			target->SetHP(newHP, HurtTypeWeapon, MakeVector3(0, 0, 0));
		}

		void EditorNetClient::SendGrenade(const Grenade& grenade) {
			SPADES_MARK_FUNCTION();
			if (!client || !client->GetWorld())
				return;

			World* world = client->GetWorld();
			// Create a grenade entity in the world
			// The grenade will be handled by the game physics system
			auto grenadePtr = std::make_unique<Grenade>(grenade);
			world->AddGrenade(std::move(grenadePtr));
		}

		void EditorNetClient::SendBlockLine(IntVector3 v1, IntVector3 v2) {
			SPADES_MARK_FUNCTION();
			if (!map)
				return;

			int dx = abs(v2.x - v1.x);
			int dy = abs(v2.y - v1.y);
			int dz = abs(v2.z - v1.z);

			int sx = (v1.x < v2.x) ? 1 : -1;
			int sy = (v1.y < v2.y) ? 1 : -1;
			int sz = (v1.z < v2.z) ? 1 : -1;

			// 3D Bresenham algorithm: track error terms for each axis
			int err_x = dx - dy - dz;
			int err_y = dy - dx - dz;
			int err_z = dz - dx - dy;

			IntVector3 pos = v1;
			while (true) {
				SendBlockAction(pos, BlockActionDig);

				if (pos.x == v2.x && pos.y == v2.y && pos.z == v2.z)
					break;

				// Find which axis has the largest error and step that direction
				int abs_err_x = abs(err_x + dy + dz);
				int abs_err_y = abs(err_y + dx + dz);
				int abs_err_z = abs(err_z + dx + dy);

				if (abs_err_x > abs_err_y && abs_err_x > abs_err_z) {
					err_x -= 2 * (dy + dz);
					err_y += 2 * dx;
					err_z += 2 * dx;
					pos.x += sx;
				} else if (abs_err_y > abs_err_z) {
					err_x += 2 * dy;
					err_y -= 2 * (dx + dz);
					err_z += 2 * dy;
					pos.y += sy;
				} else {
					err_x += 2 * dz;
					err_y += 2 * dz;
					err_z -= 2 * (dx + dy);
					pos.z += sz;
				}
			}
		}

		void EditorNetClient::UpdatePlayerState() {
			SPADES_MARK_FUNCTION();
			if (!client || !client->GetWorld())
				return;

			World* world = client->GetWorld();
			auto player = world->GetPlayer(localPlayerId);

			if (!player)
				return;

			Player& p = *player;

			// Track player position
			lastPlayerPos = p.GetPosition();

			// Instant respawn on death
			if (p.GetHealth() <= 0) {
				p.SetHP(100, HurtTypeWeapon, MakeVector3(0, 0, 0));
				p.SetPosition(lastPlayerPos);
			}
		}

		void EditorNetClient::DoEvents(float dt) {
			SPADES_MARK_FUNCTION();
			if (status != NetClientStatusConnected)
				return;
		}

		void EditorNetClient::InitializeLocalPlayer() {
			SPADES_MARK_FUNCTION();
			if (!client || !map)
				return;

			// Create basic game properties for the world
			if (!properties) {
				properties = std::make_shared<GameProperties>(ProtocolVersion::v076);
			}

			// Create the world
			World* world = new World(properties);
			world->SetMap(map);

			// Set up game mode (TC for simplicity)
			auto gameMode = stmp::make_unique<TCGameMode>(*world);
			world->SetMode(std::move(gameMode));

			// Set up teams with default names and colors
			World::Team& team0 = world->GetTeam(0);
			World::Team& team1 = world->GetTeam(1);

			team0.name = "Blue";
			team1.name = "Green";
			team0.color = MakeIntVector3(0, 0, 255);
			team1.color = MakeIntVector3(0, 255, 0);

			// Set fog color (sky blue)
			world->SetFogColor(MakeIntVector3(128, 156, 200));

			client->SetWorld(world);

			// Create a local player in spectator mode
			localPlayerId = 0;
			auto player = std::make_unique<Player>(*world, localPlayerId, RIFLE_WEAPON, 2);

			// Set player position and orientation
			if (map) {
				Vector3 centerPos(map->Width() / 2.0f, map->Height() / 2.0f, 32.0f);
				player->SetPosition(centerPos);
				player->SetOrientation(MakeVector3(0.0f, 1.0f, 0.0f));
			}

			world->SetPlayer(localPlayerId, std::move(player));
			world->SetLocalPlayerIndex(localPlayerId);

			// Signal that the game is ready (sets up camera and UI)
			client->JoinedGame();
		}
	} // namespace client
} // namespace spades
