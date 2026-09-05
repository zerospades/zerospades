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

#include <cstdlib>

#include "Client.h"

#include <Core/Settings.h>
#include <Core/Strings.h>

#include "IAudioChunk.h"
#include "IAudioDevice.h"

#include "CTFGameMode.h"
#include "GameProperties.h"
#include "IGameMode.h"
#include "TCGameMode.h"

#include "CenterMessageView.h"
#include "ChatWindow.h"
#include "ClientUI.h"
#include "ExtendedTeamplay.h"
#include "LimboView.h"

#include "GameMap.h"
#include "World.h"

#include "DemoRecorder.h"
#include "NetClient.h"

DEFINE_SPADES_SETTING(cg_corpseClearOnRespawn, "0");
DEFINE_SPADES_SETTING(cg_centerMessage, "2");
DEFINE_SPADES_SETTING(cg_autoScreenshot, "0");
DEFINE_SPADES_SETTING(cg_demoAutoRecord, "0");
DEFINE_SPADES_SETTING(cg_demoMaxFiles, "10");
DEFINE_SPADES_SETTING(cg_demoAutoPrune, "1");

namespace spades {
	extern std::string g_pendingMapName;
	extern std::string g_pendingServerName;

	namespace client {

#pragma mark - Server Packet Handlers

		std::string Client::BuildDemoContext() {
			std::string modeName = "game";
			if (world) {
				auto gmode = world->GetMode();
				if (gmode) {
					switch (gmode->ModeType()) {
						case IGameMode::m_CTF: modeName = "ctf"; break;
						case IGameMode::m_TC: modeName = "tc"; break;
					}
				}
			}
			if (activeNet && activeNet->GetStatus() == NetClientStatusConnected &&
			    activeNet->GetGameProperties()->isGameModeArena)
				modeName = "arena";

			// Field order: server-map-gamemode
			// Each field is sanitized so '-' never appears within a field.
			std::string ctx;
			if (!g_pendingServerName.empty())
				ctx += DemoRecorder::SanitizeComponent(g_pendingServerName) + "-";
			if (!g_pendingMapName.empty())
				ctx += DemoRecorder::SanitizeComponent(g_pendingMapName) + "-";
			ctx += modeName;
			return ctx;
		}

		void Client::LocalPlayerCreated() {
			Player& p = world->GetLocalPlayer().value();

			// clear hurt/heal effects
			lastHurtTime = -100.0F;
			lastHealTime = -100.0F;
			damageTaken = 0;
			lastHealth = p.GetHealth();

			// reset input
			playerInput.jump = PlayerInput().jump;
			weapInput = WeaponInput();

			hotBarIconState = 1.0F;

			// set loadout
			limbo->SetSelectedTeam(p.GetTeamId());
			limbo->SetSelectedWeapon(p.GetWeaponType());

			// update minimap
			renderer->UpdateFlatGameMap();
		}

		void Client::JoinedGame() {
			// Note: A localplayer doesn't exist yet

			// reset input
			playerInput = PlayerInput();
			weapInput = WeaponInput();

			// prepare the spectator view
			followCameraState.enabled = false;

			// get highest solid block at map's center
			if (map) {
				IntVector3 mapPos;
				mapPos.x = map->Width() / 2;
				mapPos.y = map->Height() / 2;
				mapPos.z = map->GetTop(mapPos.x, mapPos.y) - map->Depth();

				freeCameraState.position.x = static_cast<float>(mapPos.x);
				freeCameraState.position.y = static_cast<float>(mapPos.y);
				freeCameraState.position.z = static_cast<float>(mapPos.z);
			} else {
				// Map not loaded yet, use default position
				freeCameraState.position = MakeVector3(256.0F, 256.0F, 5.0F);
			}
			freeCameraState.velocity = MakeVector3(0, 0, 0);
			followAndFreeCameraState.yaw = DEG2RAD(90);
			followAndFreeCameraState.pitch = DEG2RAD(89);

			// welcome players (skip during demo playback)
			if (!IsDemoMode())
				centerMessageView->AddMessage(_Tr("Client", "Welcome to the server, {0}!", playerName));

			// play intro sound
			Handle<IAudioChunk> c = audioDevice->RegisterSound("Sounds/Feedback/Intro.opus");
			audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());

			// display client version
			std::string verStr = std::string(ZEROSPADES_VER_STR);
			std::string osInfo = VersionInfo::GetVersionInfo();
			std::string archInfo = VersionInfo::GetAppArchitecture();
			std::string s = _Tr("Client", "You are connected with {0} ({2}) on {1}", verStr, osInfo, archInfo);
			chatWindow->AddMessage(ChatWindow::ColoredMessage(s, MsgColorSysInfo));

			// start recording if auto-record is enabled
			if (net && (int)cg_demoAutoRecord != 0) {
				if (net->StartDemoRecording("", BuildDemoContext())) {
					const auto demoPath = net->GetDemoFilename().c_str();
					SPLog("Started auto-recording demo: %s", demoPath);
					ShowAlert(_Tr("Client", "Recording demo: {0}", demoPath), AlertType::Notice);
					if ((int)cg_demoAutoPrune != 0) {
						int maxDemos = (int)cg_demoMaxFiles;
						if (maxDemos >= 1)
							DemoRecorder::PruneOldRecordings(static_cast<size_t>(maxDemos));
					}
				}
			}
		}

		void Client::TeamCapturedTerritory(int teamId, int terId) {
			auto& tr = dynamic_cast<TCGameMode&>(*world->GetMode()).GetTerritory(terId);
			int old = tr.ownerTeamId;

			std::string msg;
			std::string teamName = world->GetTeamName(teamId);
			std::string oldTeamName = world->GetTeamName(old);

			{
				if (old >= NEUTRAL_TEAM) {
					msg = _Tr("Client", "{0} captured a Neutral Territory", teamName);
				} else {
					msg = _Tr("Client", "{0} captured {1}'s Territory", teamName, oldTeamName);
				}
				NetLog("%s", msg.c_str());
				if (cg_centerMessage)
					centerMessageView->AddMessage(msg);
			}
			{
				teamName = chatWindow->TeamColorMessage(teamName, teamId);
				if (old >= NEUTRAL_TEAM) {
					msg = _Tr("Client", "{0} captured a neutral territory", teamName);
				} else {
					oldTeamName = chatWindow->TeamColorMessage(oldTeamName, old);
					msg = _Tr("Client", "{0} captured {1}'s territory", teamName, oldTeamName);
				}
				chatWindow->AddMessage(msg);
			}

			stmp::optional<Player&> localplayer = world->GetLocalPlayer();
			if (localplayer && !IsMuted()) {
				Handle<IAudioChunk> c = (teamId == localplayer->GetTeamId())
					? audioDevice->RegisterSound("Sounds/Feedback/TC/YourTeamCaptured.opus")
					: audioDevice->RegisterSound("Sounds/Feedback/TC/EnemyCaptured.opus");
				audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
			}
		}

		void Client::PlayerCapturedIntel(Player& p) {
			std::string msg;

			int teamId = p.GetTeamId();
			int otherTeamId = 1 - teamId;
			std::string teamName = world->GetTeamName(otherTeamId);

			{
				msg = _Tr("Client", "{0} captured {1}'s Intel", p.GetName(), teamName);
				NetLog("%s", msg.c_str());
				if (cg_centerMessage) {
					if (p.IsLocalPlayer())
						msg = _Tr("Client", "You captured {0}'s Intel", teamName);
					centerMessageView->AddMessage(msg);
				}
			}
			{
				msg = _Tr("Client", "{0} captured {1}'s intel",
						  chatWindow->TeamColorMessage(p.GetName(), teamId),
						  chatWindow->TeamColorMessage(teamName, otherTeamId));
				chatWindow->AddMessage(msg);
			}

			stmp::optional<Player&> localplayer = world->GetLocalPlayer();
			if (localplayer && !IsMuted()) {
				Handle<IAudioChunk> c = (teamId == localplayer->GetTeamId())
					? audioDevice->RegisterSound("Sounds/Feedback/CTF/YourTeamCaptured.opus")
					: audioDevice->RegisterSound("Sounds/Feedback/CTF/EnemyCaptured.opus");
				audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
			}
		}

		void Client::PlayerPickedIntel(Player& p) {
			std::string msg;

			int teamId = p.GetTeamId();
			int otherTeamId = 1 - teamId;
			std::string teamName = world->GetTeamName(otherTeamId);

			{
				msg = _Tr("Client", "{0} picked up {1}'s Intel", p.GetName(), teamName);
				NetLog("%s", msg.c_str());
				if (cg_centerMessage) {
					if (p.IsLocalPlayer())
						msg = _Tr("Client", "You picked up {0}'s Intel", teamName);
					centerMessageView->AddMessage(msg);
				}
			}
			{
				msg = _Tr("Client", "{0} picked up {1}'s intel",
						  chatWindow->TeamColorMessage(p.GetName(), teamId),
						  chatWindow->TeamColorMessage(teamName, otherTeamId));
				chatWindow->AddMessage(msg);
			}

			if (!IsMuted()) {
				Handle<IAudioChunk> c =
				  audioDevice->RegisterSound("Sounds/Feedback/CTF/PickedUp.opus");
				audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
			}
		}

		void Client::PlayerDropIntel(Player& p) {
			std::string msg;

			int teamId = p.GetTeamId();
			int otherTeamId = 1 - teamId;
			std::string teamName = world->GetTeamName(otherTeamId);

			{
				msg = _Tr("Client", "{0} dropped {1}'s Intel", p.GetName(), teamName);
				NetLog("%s", msg.c_str());
				if (cg_centerMessage) {
					if (p.IsLocalPlayer())
						msg = _Tr("Client", "You dropped {0}'s Intel", teamName);
					centerMessageView->AddMessage(msg);
				}
			}
			{
				msg = _Tr("Client", "{0} dropped {1}'s intel",
						  chatWindow->TeamColorMessage(p.GetName(), teamId),
						  chatWindow->TeamColorMessage(teamName, otherTeamId));
				chatWindow->AddMessage(msg);
			}
		}

		void Client::PlayerCreatedBlock(Player& p) {
			SPADES_MARK_FUNCTION();

			if (!IsMuted()) {
				Handle<IAudioChunk> c = audioDevice->RegisterSound("Sounds/Weapons/Block/Build.opus");
				audioDevice->Play(c.GetPointerOrNull(), p.GetEye() + p.GetFront(), AudioParam());
			}
		}

		void Client::PlayBlockDestroySound(spades::Vector3 pos) {
			SPADES_MARK_FUNCTION();

			if (!IsMuted()) {
				Handle<IAudioChunk> c = audioDevice->RegisterSound("Sounds/Misc/BlockDestroy.opus");
				audioDevice->Play(c.GetPointerOrNull(), pos, AudioParam());
			}
		}

		void Client::PlayerDestroyedBlockWithWeaponOrTool(spades::IntVector3 pos) {
			SPAssert(map);

			if (!map->IsSolid(pos.x, pos.y, pos.z))
				return;

			PlayBlockDestroySound(MakeVector3(pos) + 0.5F);
			EmitBlockDestroyFragments(pos);
		}

		void Client::PlayerDiggedBlock(spades::IntVector3 pos) {
			SPAssert(map);

			PlayBlockDestroySound(MakeVector3(pos) + 0.5F);

			for (int z = pos.z - 1; z <= pos.z + 1; z++) {
				if (z < 0 || z >= map->GroundDepth())
					continue;
				if (!map->IsSolid(pos.x, pos.y, z))
					continue;

				EmitBlockDestroyFragments(MakeIntVector3(pos.x, pos.y, z));
			}
		}

		void Client::GrenadeDestroyedBlock(spades::IntVector3 pos) {
			SPAssert(map);

			for (int x = pos.x - 1; x <= pos.x + 1; x++)
			for (int y = pos.y - 1; y <= pos.y + 1; y++)
			for (int z = pos.z - 1; z <= pos.z + 1; z++) {
				if (!map->IsValidMapCoord(x, y, z) || z >= map->GroundDepth())
					continue;
				if (!map->IsSolid(x, y, z))
					continue;

				EmitBlockDestroyFragments(MakeIntVector3(x, y, z));
			}
		}

		void Client::PlayerLeaving(Player& p) {
			int playerId = p.GetId();

			// A mark or a ping outlives its owner otherwise, and the slot can be reused
			// by whoever connects next.
			teamplay->PlayerLeft(playerId);

			// Choose the next player if a follow cam is active on this player
			if (FollowsNonLocalPlayer(GetCameraMode()) && GetCameraTargetPlayerId() == playerId) {
				FollowNextPlayer(false);

				// Still unable to find a substitute?
				if (GetCameraTargetPlayerId() == playerId)
					followCameraState.enabled = false; // Turn off the follow cam mode
			}

			// check if the player was being dominated
			bool wasDominated = false;
			for (auto it = killStreaks.begin(); it != killStreaks.end(); ++it) {
				if (it->second.find(playerId) != it->second.end() && it->second[playerId] >= 4) {
					wasDominated = true;
					break;
				}
			}

			std::string msg;
			{
				msg = _Tr("Client", "Player {0} has left", p.GetName());
				if (wasDominated)
					msg += _Tr("Client", " (ragequit)");
				NetLog("%s", msg.c_str());
				scriptedUI->RecordChatLog(msg, ConvertColorRGBA(p.GetColor()));
			}
			{
				msg = chatWindow->TeamColorMessage(p.GetName(), p.GetTeamId());
				msg = _Tr("Client", "Player {0} has left", msg);
				if (wasDominated)
					msg += _Tr("Client", " (ragequit)");
				chatWindow->AddMessage(msg);
			}

			// remove player streaks from the killStreaks map
			for (auto it = killStreaks.begin(); it != killStreaks.end();) {
				it->second.erase(playerId);
				if (it->first == playerId) {
					it = killStreaks.erase(it);
				} else {
					++it;
				}
			}

			RemoveCorpseForPlayer(playerId);
		}

		void Client::PlayerJoinedTeam(Player& p) {
			std::string msg;
			std::string teamName = p.IsSpectator()
				? _Tr("Client", "Spectator") : p.GetTeamName();
			
			if (p.IsLocalPlayer()) {
				msg = p.IsSpectator()
					? _Tr("Client", "You are now a spectator")
					: _Tr("Client", "You joined {0} team", teamName);
				centerMessageView->AddMessage(msg);
				return;
			}
			
			{
				msg = _Tr("Client", "{0} joined {1} team", p.GetName(), teamName);
				NetLog("%s", msg.c_str());
				scriptedUI->RecordChatLog(msg, ConvertColorRGBA(p.GetColor()));
			}
			{
				msg = chatWindow->TeamColorMessage(teamName, p.GetTeamId());
				msg = _Tr("Client", "{0} joined {1} team", p.GetName(), msg);
				chatWindow->AddMessage(msg);
			}
		}

		void Client::PlayerSpawned(Player& p) {
			// An ESP Mark with CLEAR_ON_RESPAWN ends here rather than at the death, so
			// any Create Player for the id ends it and a mark on a player who stays dead
			// lasts until they come back. Without the flag it survives death and respawn,
			// so a punishment mark need not be re-sent on every kill.
			ExtendedTeamplayPlayerSpawned(p.GetId());

			bool isArena = activeNet && activeNet->GetStatus() == NetClientStatusConnected &&
			               activeNet->GetGameProperties()->isGameModeArena;
			if (isArena || cg_corpseClearOnRespawn)
				RemoveCorpseForPlayer(p.GetId());
		}

		void Client::TeamWon(int teamId) {
			// take a screenshot of the scoreboard
			if (cg_autoScreenshot && !IsDemoMode())
				TakeScreenShot(false, true);

			std::string msg;
			std::string teamName = world->GetTeamName(teamId);

			{
				msg = _Tr("Client", "{0} Wins!", teamName);
				NetLog("%s", msg.c_str());
				scriptedUI->RecordChatLog(msg);
				centerMessageView->AddMessage(msg);
			}
			{
				msg = chatWindow->TeamColorMessage(teamName, teamId);
				msg = _Tr("Client", "{0} wins!", msg);
				chatWindow->AddMessage(msg);
			}

			stmp::optional<Player&> localplayer = world->GetLocalPlayer();
			if (localplayer) {
				Handle<IAudioChunk> c = (teamId == localplayer->GetTeamId())
					? audioDevice->RegisterSound("Sounds/Feedback/Win.opus")
					: audioDevice->RegisterSound("Sounds/Feedback/Lose.opus");
				audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
			}
		}

		void Client::MarkWorldUpdate() { upsCounter.MarkFrame(); }
	} // namespace client
} // namespace spades