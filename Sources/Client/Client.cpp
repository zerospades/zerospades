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

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>

#include "Client.h"
#include "Fonts.h"
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/Settings.h>
#include <Core/Strings.h>

#include "IAudioChunk.h"
#include "IAudioDevice.h"

#include "CenterMessageView.h"
#include "ChatWindow.h"
#include "ClientPlayer.h"
#include "ClientUI.h"
#include "HurtRingView.h"
#include "LimboView.h"
#include "MapView.h"
#include "PaletteView.h"
#include "PieMenuView.h"
#include "ScoreboardView.h"
#include "TCProgressView.h"

#include "BloodMarks.h"
#include "Corpse.h"
#include "SmokeSpriteEntity.h"

#include "GameMap.h"
#include "Weapon.h"
#include "World.h"

#include "NetClient.h"

DEFINE_SPADES_SETTING(cg_chatBeep, "1");
DEFINE_SPADES_SETTING(cg_alerts, "1");
DEFINE_SPADES_SETTING(cg_alertSounds, "1");
DEFINE_SPADES_SETTING(cg_serverAlert, "1");
DEFINE_SPADES_SETTING(cg_skipDeadPlayersWhenDead, "0");
DEFINE_SPADES_SETTING(cg_ignorePrivateMessages, "0");
DEFINE_SPADES_SETTING(cg_ignoreChatMessages, "0");
DEFINE_SPADES_SETTING(cg_smallFont, "0");

SPADES_SETTING(cg_playerName);
SPADES_SETTING(cg_centerMessageSmallFont);

namespace spades {
	namespace client {

		Client::Client(Handle<IRenderer> r, Handle<IAudioDevice> audioDev,
					   const ServerAddress& host, Handle<FontManager> fontManager,
					   const std::string& demoPath)
			: activeNet(nullptr),
			  demoFilePath(demoPath),
			  playerName(StripNewlines(cg_playerName.operator std::string()).substr(0, 15)),
			  logStream(nullptr),
			  hostname(host),
			  renderer(r),
			  audioDevice(audioDev),

			  time(0.0F),
			  readyToClose(false),

			  worldSubFrame(0.0F),
			  worldSubFrameFast(0.0F),
			  frameToRendererInit(5),
			  timeSinceInit(0.0F),
			  hasLastTool(false),
			  lastPosSentTime(0.0F),
			  lastOriSentTime(0.0F),
			  lastAliveTime(0.0F),
			  lastRespawnCount(0),
			  damageTaken(0),
			  lastScore(0),
			  curKills(0),
			  curDeaths(0),
			  curStreak(0),
			  bestStreak(0),
			  meleeKills(0),
			  grenadeKills(0),
			  placedBlocks(0),
			  reloadKeyPressed(false),
			  localFireVibrationTime(-1.0F),
			  grenadeVibration(0.0F),
			  grenadeVibrationSlow(0.0F),
			  scoreboardVisible(false),
			  hudVisible(true),
			  flashlightOn(false),
			  isChristmasOn(false),
			  lastSnowDropTime(0.0F),
			  hotBarIconState(0.0F),
			  hitFeedbackIconState(0.0F),
			  hitFeedbackFriendly(false),
			  debugHitTestZoomState(0.0F),
			  debugHitTestZoom(false),
			  spectatorZoomState(0.0F),
			  spectatorZoom(false),
			  focalLength(20.0F),
			  targetFocalLength(20.0F),
			  autoFocusEnabled(true),
			  followedPlayerId(0),
			  inGameLimbo(false),
			  fontManager(fontManager),
			  alertDisappearTime(-10000.0F),
			  lastLocalCorpse(nullptr),
			  nextScreenShotIndex(0),
			  nextMapShotIndex(0),
			  staffSpectating(false),
			  spectatorPlayerNames(true) {
			SPADES_MARK_FUNCTION();
			SPLog("Initializing...");

			renderer->SetFogColor(MakeVector3(0, 0, 0));
			renderer->SetFogDistance(128.0F);

			auto* chatFont = cg_smallFont ? &fontManager->GetSmallFont() : &fontManager->GetGuiFont();
			auto* centerFont = cg_centerMessageSmallFont ? &fontManager->GetMediumFont() : &fontManager->GetLargeFont();

			chatWindow = stmp::make_unique<ChatWindow>(this, chatFont, false);
			killfeedWindow = stmp::make_unique<ChatWindow>(this, chatFont, true);

			hurtRingView = stmp::make_unique<HurtRingView>(this);
			centerMessageView = stmp::make_unique<CenterMessageView>(this, centerFont);
			mapView = stmp::make_unique<MapView>(this, false);
			largeMapView = stmp::make_unique<MapView>(this, true);
			scoreboard = stmp::make_unique<ScoreboardView>(this);
			limbo = stmp::make_unique<LimboView>(this);
			paletteView = stmp::make_unique<PaletteView>(this);
			pieMenuView = stmp::make_unique<PieMenuView>(this, chatFont,
				&fontManager->GetHeadingFont());
			tcView = stmp::make_unique<TCProgressView>(*this);
			scriptedUI = Handle<ClientUI>::New(renderer.GetPointerOrNull(),
				audioDev.GetPointerOrNull(), fontManager.GetPointerOrNull(), this);

			renderer->SetGameMap(nullptr);
		}

		void Client::SetWorld(spades::client::World* w) {
			SPADES_MARK_FUNCTION();

			if (world.get() == w)
				return;

			scriptedUI->CloseUI();

			RemoveAllLocalEntities();

			lastHealth = 100;
			damageTaken = 0;
			lastHurtTime = -100.0F;
			lastHealTime = -100.0F;
			lastHitTime = -100.0F;
			hurtRingView->ClearAll();
			killStreaks.clear();

			// reset on new map
			placedBlocks = 0;

			// In demo replay, every seek calls SetWorld via ResetWorldForReplay;
			// preserve the staff/ESP toggle across seeks.
			if (!IsDemoMode())
				staffSpectating = false;
			reloadKeyPressed = false;
			scoreboardVisible = false;
			flashlightOn = false;
			debugHitTestZoom = false;
			spectatorZoom = false;
			largeMapView->SetZoom(false);

			clientPlayers.clear();

			if (world) {
				world->SetListener(nullptr);
				renderer->SetGameMap(nullptr);
				audioDevice->SetGameMap(nullptr);
				world = nullptr;
				map = nullptr;
			}
			world.reset(w);
			if (world) {
				SPLog("World set");

				// initialize player view objects
				auto slots = world->GetNumPlayerSlots();
				clientPlayers.resize(slots);
				for (size_t i = 0; i < slots; i++) {
					auto p = world->GetPlayer(static_cast<unsigned int>(i));
					if (p)
						clientPlayers[i] = Handle<ClientPlayer>::New(*p, *this);
					else
						clientPlayers[i] = nullptr;
				}

				world->SetListener(this);
				map = world->GetMap();
				renderer->SetGameMap(map);
				audioDevice->SetGameMap(map.GetPointerOrNull());
				NetLog("------ World Loaded ------");
			} else {
				SPLog("World removed");
				NetLog("------ World Unloaded ------");
			}

			limbo->SetSelectedTeam(2);
			limbo->SetSelectedWeapon(RIFLE_WEAPON);
			inGameLimbo = false;

			worldSubFrame = 0.0F;
			worldSubFrameFast = 0.0F;
			worldSetTime = time;
		}

		Client::ViewState Client::SaveViewState() const {
			ViewState s;
			s.followEnabled = followCameraState.enabled;
			s.followFirstPerson = followCameraState.firstPerson;
			s.followedPlayerId = followedPlayerId;
			s.freePosition = freeCameraState.position;
			s.freeVelocity = freeCameraState.velocity;
			s.yaw = followAndFreeCameraState.yaw;
			s.pitch = followAndFreeCameraState.pitch;
			s.worldSetTime = worldSetTime;
			return s;
		}

		void Client::RestoreViewState(const ViewState& s) {
			followCameraState.enabled = s.followEnabled;
			followCameraState.firstPerson = s.followFirstPerson;
			followedPlayerId = s.followedPlayerId;
			freeCameraState.position = s.freePosition;
			freeCameraState.velocity = s.freeVelocity;
			followAndFreeCameraState.yaw = s.yaw;
			followAndFreeCameraState.pitch = s.pitch;
			worldSetTime = s.worldSetTime;
		}

		void Client::ReloadDemo() {
			if (!IsDemoMode() || demoFilePath.empty())
				return;

			SetWorld(nullptr);
			demoNet.reset();

			demoNet = stmp::make_unique<DemoNetClient>(this);
			if (!demoNet->OpenDemo(demoFilePath))
				SPRaise("Failed to reload demo file: %s", demoFilePath.c_str());
			activeNet = demoNet.get();
		}

		Client::~Client() {
			SPADES_MARK_FUNCTION();

			NetLog("Disconnecting");

			if (logStream) {
				SPLog("Closing netlog");
				logStream.reset();
			}

			if (net) {
				SPLog("Disconnecting");
				net->Disconnect();
				net.reset();
			}

			if (demoNet) {
				SPLog("Closing demo playback");
				demoNet.reset();
			}

			SPLog("Disconnected");

			RemoveAllLocalEntities();
			bloodMarks.reset();
			
			renderer->SetGameMap(nullptr);
			audioDevice->SetGameMap(nullptr);

			clientPlayers.clear();

			scriptedUI->ClientDestroyed();
			tcView.reset();
			limbo.reset();
			scoreboard.reset();
			mapView.reset();
			largeMapView.reset();
			chatWindow.reset();
			killfeedWindow.reset();
			paletteView.reset();
			centerMessageView.reset();
			hurtRingView.reset();
			world.reset();
		}

		/** Initiate an initialization which likely to take some time */
		void Client::DoInit() {
			renderer->Init();

			bloodMarks = stmp::make_unique<BloodMarks>(*this);

			// load images
			SmokeSpriteEntity::Preload(renderer.GetPointerOrNull());

			renderer->RegisterImage("Gfx/Bullet/7.62mm.png");
			renderer->RegisterImage("Gfx/Bullet/9mm.png");
			renderer->RegisterImage("Gfx/Bullet/12gauge.png");
			renderer->RegisterImage("Gfx/Demo/Pause.png");
			renderer->RegisterImage("Gfx/Demo/Play.png");
			renderer->RegisterImage("Gfx/Demo/Recording.png");
			renderer->RegisterImage("Gfx/Hotbar/Block.png");
			renderer->RegisterImage("Gfx/Hotbar/Grenade.png");
			renderer->RegisterImage("Gfx/Hotbar/Spade.png");
			renderer->RegisterImage("Gfx/Hotbar/Rifle.png");
			renderer->RegisterImage("Gfx/Hotbar/SMG.png");
			renderer->RegisterImage("Gfx/Hotbar/Shotgun.png");
			renderer->RegisterImage("Gfx/Killfeed/a-Rifle.png");
			renderer->RegisterImage("Gfx/Killfeed/b-SMG.png");
			renderer->RegisterImage("Gfx/Killfeed/c-Shotgun.png");
			renderer->RegisterImage("Gfx/Killfeed/d-Headshot.png");
			renderer->RegisterImage("Gfx/Killfeed/e-Melee.png");
			renderer->RegisterImage("Gfx/Killfeed/f-Grenade.png");
			renderer->RegisterImage("Gfx/Killfeed/g-Falling.png");
			renderer->RegisterImage("Gfx/Killfeed/h-Teamchange.png");
			renderer->RegisterImage("Gfx/Killfeed/i-Classchange.png");
			renderer->RegisterImage("Gfx/Killfeed/j-Airborne.png");
			renderer->RegisterImage("Gfx/Killfeed/k-Noscope.png");
			renderer->RegisterImage("Gfx/Killfeed/l-Domination.png");
			renderer->RegisterImage("Gfx/Killfeed/m-Revenge.png");
			renderer->RegisterImage("Gfx/Ball.png");
			renderer->RegisterImage("Gfx/HurtRing.png");
			renderer->RegisterImage("Gfx/HurtSprite.png");
			renderer->RegisterImage("Gfx/ReflexSight.png");
			renderer->RegisterImage("Gfx/Spotlight.jpg");
			renderer->RegisterImage("Gfx/White.tga");
			renderer->RegisterImage("Textures/Fluid.png");
			renderer->RegisterImage("Textures/WaterExpl.png");

			// load sounds
			LoadKillSounds();

			audioDevice->RegisterSound("Sounds/Feedback/CTF/EnemyCaptured.opus");
			audioDevice->RegisterSound("Sounds/Feedback/CTF/PickedUp.opus");
			audioDevice->RegisterSound("Sounds/Feedback/CTF/YourTeamCaptured.opus");
			audioDevice->RegisterSound("Sounds/Feedback/TC/EnemyCaptured.opus");
			audioDevice->RegisterSound("Sounds/Feedback/TC/YourTeamCaptured.opus");
			audioDevice->RegisterSound("Sounds/Feedback/Alert.opus");
			audioDevice->RegisterSound("Sounds/Feedback/Beep1.opus");
			audioDevice->RegisterSound("Sounds/Feedback/Beep2.opus");
			audioDevice->RegisterSound("Sounds/Feedback/Chat.opus");
			audioDevice->RegisterSound("Sounds/Feedback/HeadshotFeedback.opus");
			audioDevice->RegisterSound("Sounds/Feedback/HitFeedback.opus");
			audioDevice->RegisterSound("Sounds/Feedback/Lose.opus");
			audioDevice->RegisterSound("Sounds/Feedback/Win.opus");
			audioDevice->RegisterSound("Sounds/Misc/BlockBounce.opus");
			audioDevice->RegisterSound("Sounds/Misc/BlockDestroy.opus");
			audioDevice->RegisterSound("Sounds/Misc/BlockFall.opus");
			audioDevice->RegisterSound("Sounds/Misc/CloseMap.opus");
			audioDevice->RegisterSound("Sounds/Misc/OpenMap.opus");
			audioDevice->RegisterSound("Sounds/Misc/SwitchMapZoom.opus");
			audioDevice->RegisterSound("Sounds/Player/Death.opus");
			audioDevice->RegisterSound("Sounds/Player/FallHurt.opus");
			audioDevice->RegisterSound("Sounds/Player/Flashlight.opus");
			audioDevice->RegisterSound("Sounds/Player/Footstep1.opus");
			audioDevice->RegisterSound("Sounds/Player/Footstep2.opus");
			audioDevice->RegisterSound("Sounds/Player/Footstep3.opus");
			audioDevice->RegisterSound("Sounds/Player/Footstep4.opus");
			audioDevice->RegisterSound("Sounds/Player/Jump.opus");
			audioDevice->RegisterSound("Sounds/Player/Land.opus");
			audioDevice->RegisterSound("Sounds/Player/Run1.opus");
			audioDevice->RegisterSound("Sounds/Player/Run2.opus");
			audioDevice->RegisterSound("Sounds/Player/Run3.opus");
			audioDevice->RegisterSound("Sounds/Player/Run4.opus");
			audioDevice->RegisterSound("Sounds/Player/Run5.opus");
			audioDevice->RegisterSound("Sounds/Player/Run6.opus");
			audioDevice->RegisterSound("Sounds/Player/Wade1.opus");
			audioDevice->RegisterSound("Sounds/Player/Wade2.opus");
			audioDevice->RegisterSound("Sounds/Player/Wade3.opus");
			audioDevice->RegisterSound("Sounds/Player/Wade4.opus");
			audioDevice->RegisterSound("Sounds/Player/WaterJump.opus");
			audioDevice->RegisterSound("Sounds/Player/WaterLand.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Block/Build.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Block/RaiseLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/Bounce.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/Debris.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/DropWater.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/Explode1.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/Explode2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/ExplodeFar.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/ExplodeFarStereo.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/ExplodeStereo1.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/ExplodeStereo2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/Fire.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/RaiseLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/Throw.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/WaterExplode.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/WaterExplodeFar.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Grenade/WaterExplodeStereo.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/Block.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/Flesh1.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/Flesh2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/Flesh3.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/FleshLocal1.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/FleshLocal2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/FleshLocal3.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/FleshLocal4.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/Ricochet1.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/Ricochet2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/Ricochet3.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/Ricochet4.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/Water1.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/Water2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/Water3.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Impacts/Water4.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/Fire1.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/Fire2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/Fire3.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/FireFar.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/FireLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/FireStereo.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/RaiseLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/Reload.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/ReloadLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/ShellDrop1.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/ShellDrop2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/ShellWater.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/V2AmbienceLarge.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Rifle/V2AmbienceSmall.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Shotgun/Cock.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Shotgun/CockLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Shotgun/Fire.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Shotgun/FireFar.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Shotgun/FireLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Shotgun/FireStereo.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Shotgun/RaiseLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Shotgun/Reload.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Shotgun/ReloadLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Shotgun/V2AmbienceLarge.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Shotgun/V2AmbienceSmall.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/FireFar.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/FireStereo.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/RaiseLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/Reload.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/ReloadLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/ShellDrop1.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/ShellDrop2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/ShellWater.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2AmbienceLarge1.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2AmbienceLarge2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2AmbienceLarge2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2AmbienceLarge3.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2AmbienceLarge4.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2AmbienceSmall1.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2AmbienceSmall2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2AmbienceSmall2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2AmbienceSmall3.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2AmbienceSmall4.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2Local1.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2Local2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2Local3.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2Local4.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2Third1.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2Third2.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2Third3.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SMG/V2Third4.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Spade/HitBlock.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Spade/HitPlayer.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Spade/Miss.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Spade/RaiseLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/AimDownSightLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/DryFire.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Restock.opus");
			audioDevice->RegisterSound("Sounds/Weapons/RestockLocal.opus");
			audioDevice->RegisterSound("Sounds/Weapons/Switch.opus");
			audioDevice->RegisterSound("Sounds/Weapons/SwitchLocal.opus");

			// load models
			renderer->RegisterModel("Models/MapObjects/BlockCursorLine.kv6");
			renderer->RegisterModel("Models/MapObjects/CheckPoint.kv6");
			renderer->RegisterModel("Models/MapObjects/Intel.kv6");
			renderer->RegisterModel("Models/Player/Rifle/Arm.kv6");
			renderer->RegisterModel("Models/Player/Rifle/Arms.kv6");
			renderer->RegisterModel("Models/Player/Rifle/Leg.kv6");
			renderer->RegisterModel("Models/Player/Rifle/LegCrouch.kv6");
			renderer->RegisterModel("Models/Player/Rifle/Torso.kv6");
			renderer->RegisterModel("Models/Player/Rifle/TorsoCrouch.kv6");
			renderer->RegisterModel("Models/Player/Rifle/Dead.kv6");
			renderer->RegisterModel("Models/Player/Rifle/Head.kv6");
			renderer->RegisterModel("Models/Player/Rifle/UpperArm.kv6");
			renderer->RegisterModel("Models/Player/Shotgun/Arm.kv6");
			renderer->RegisterModel("Models/Player/Shotgun/Arms.kv6");
			renderer->RegisterModel("Models/Player/Shotgun/Dead.kv6");
			renderer->RegisterModel("Models/Player/Shotgun/Head.kv6");
			renderer->RegisterModel("Models/Player/Shotgun/Leg.kv6");
			renderer->RegisterModel("Models/Player/Shotgun/LegCrouch.kv6");
			renderer->RegisterModel("Models/Player/Shotgun/Torso.kv6");
			renderer->RegisterModel("Models/Player/Shotgun/TorsoCrouch.kv6");
			renderer->RegisterModel("Models/Player/Shotgun/UpperArm.kv6");
			renderer->RegisterModel("Models/Player/SMG/Arm.kv6");
			renderer->RegisterModel("Models/Player/SMG/Arms.kv6");
			renderer->RegisterModel("Models/Player/SMG/Dead.kv6");
			renderer->RegisterModel("Models/Player/SMG/Head.kv6");
			renderer->RegisterModel("Models/Player/SMG/Leg.kv6");
			renderer->RegisterModel("Models/Player/SMG/LegCrouch.kv6");
			renderer->RegisterModel("Models/Player/SMG/Torso.kv6");
			renderer->RegisterModel("Models/Player/SMG/TorsoCrouch.kv6");
			renderer->RegisterModel("Models/Player/SMG/UpperArm.kv6");
			renderer->RegisterModel("Models/Player/Arm.kv6");
			renderer->RegisterModel("Models/Player/Arms.kv6");
			renderer->RegisterModel("Models/Player/Dead.kv6");
			renderer->RegisterModel("Models/Player/Head.kv6");
			renderer->RegisterModel("Models/Player/Leg.kv6");
			renderer->RegisterModel("Models/Player/LegCrouch.kv6");
			renderer->RegisterModel("Models/Player/Torso.kv6");
			renderer->RegisterModel("Models/Player/TorsoCrouch.kv6");
			renderer->RegisterModel("Models/Player/UpperArm.kv6");
			renderer->RegisterModel("Models/Weapons/Spade/Pickaxe.kv6");
			renderer->RegisterModel("Models/Weapons/Spade/Spade.kv6");
			renderer->RegisterModel("Models/Weapons/Block/Block.kv6");
			renderer->RegisterModel("Models/Weapons/Grenade/Grenade.kv6");
			renderer->RegisterModel("Models/Weapons/Rifle/Casing.kv6");
			renderer->RegisterModel("Models/Weapons/Rifle/Magazine.kv6");
			renderer->RegisterModel("Models/Weapons/Rifle/SightFront.kv6");
			renderer->RegisterModel("Models/Weapons/Rifle/SightRear.kv6");
			renderer->RegisterModel("Models/Weapons/Rifle/SightReflex.kv6");
			renderer->RegisterModel("Models/Weapons/Rifle/Weapon.kv6");
			renderer->RegisterModel("Models/Weapons/Rifle/WeaponNoMagazine.kv6");
			renderer->RegisterModel("Models/Weapons/Shotgun/Casing.kv6");
			renderer->RegisterModel("Models/Weapons/Shotgun/Pump.kv6");
			renderer->RegisterModel("Models/Weapons/Shotgun/SightFront.kv6");
			renderer->RegisterModel("Models/Weapons/Shotgun/SightRear.kv6");
			renderer->RegisterModel("Models/Weapons/Shotgun/SightReflex.kv6");
			renderer->RegisterModel("Models/Weapons/Shotgun/Weapon.kv6");
			renderer->RegisterModel("Models/Weapons/Shotgun/WeaponNoPump.kv6");
			renderer->RegisterModel("Models/Weapons/SMG/Casing.kv6");
			renderer->RegisterModel("Models/Weapons/SMG/Magazine.kv6");
			renderer->RegisterModel("Models/Weapons/SMG/SightFrontPin.kv6");
			renderer->RegisterModel("Models/Weapons/SMG/SightFront.kv6");
			renderer->RegisterModel("Models/Weapons/SMG/SightRear.kv6");
			renderer->RegisterModel("Models/Weapons/SMG/SightReflex.kv6");
			renderer->RegisterModel("Models/Weapons/SMG/Weapon.kv6");
			renderer->RegisterModel("Models/Weapons/SMG/WeaponNoMagazine.kv6");
			renderer->RegisterModel("Models/Weapons/Charms/Charm.kv6");
			renderer->RegisterModel("Models/Weapons/Charms/CharmBase.kv6");

			// detect seasonal events & load assets
			time_t t;
			struct tm tm;
			::time(&t);
			tm = *localtime(&t);
			int month = tm.tm_mon + 1;
			int day = tm.tm_mday;
			isChristmasOn = (month == 12 && day >= 20 && day <= 27);
			if (isChristmasOn) {
				renderer->RegisterModel("Models/MapObjects/XmasTree.kv6");
				renderer->RegisterModel("Models/MapObjects/XmasGift.kv6");
				renderer->RegisterModel("Models/Player/XmasHat.kv6");
			}

			if (mumbleLink.Init())
				SPLog("Mumble linked");
			else
				SPLog("Mumble link failed");
			mumbleLink.SetContext(hostname.ToString(false));
			mumbleLink.SetIdentity(playerName);

			if (!demoFilePath.empty()) {
				SPLog("Starting demo playback: '%s'", demoFilePath.c_str());
				demoNet = stmp::make_unique<DemoNetClient>(this);
				if (!demoNet->OpenDemo(demoFilePath))
					SPRaise("Failed to open demo file: %s", demoFilePath.c_str());
				activeNet = demoNet.get();
			} else {
				SPLog("Started connecting to '%s'", hostname.ToString().c_str());
				net = stmp::make_unique<NetClient>(this);
				net->Connect(hostname);
				activeNet = net.get();
			}

			// get host/time string
			std::string fn = hostname.ToString(false);
			std::string fn2;
			{
				time_t t;
				struct tm tm;
				::time(&t);
				tm = *localtime(&t);
				char timeBuf[32];
				strftime(timeBuf, sizeof(timeBuf), "%Y%m%d%H%M%S_", &tm);
				fn2 = timeBuf;
			}
			for (size_t i = 0; i < fn.size(); i++) {
				char c = fn[i];
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
					fn2 += c;
				else
					fn2 += '_';
			}

			// decide log file name
			const std::string logFn = "NetLogs/" + fn2 + ".log";
			try {
				logStream = FileManager::OpenForWriting(logFn.c_str());
				SPLog("Netlog started at '%s'", logFn.c_str());
			} catch (const std::exception& ex) {
				SPLog("Failed to open netlog file '%s' (%s)", logFn.c_str(), ex.what());
			}
		}

		void Client::RunFrame(float dt) {
			SPADES_MARK_FUNCTION();

			fpsCounter.MarkFrame();

			// waiting for renderer initialization
			if (frameToRendererInit > 0) {
				DrawStartupScreen();

				frameToRendererInit--;
				if (frameToRendererInit == 0) {
					DoInit();
				} else {
					return;
				}
			}

			timeSinceInit += std::min(dt, 0.03F);

			// update network or demo playback
			try {
				activeNet->DoEvents(dt);
			} catch (const std::exception& ex) {
				NetClientStatus status = activeNet->GetStatus();
				if (status == NetClientStatusNotConnected) {
					SPLog("Disconnected because of error:\n%s", ex.what());
					NetLog("Disconnected because of error:\n%s", ex.what());
					throw;
				} else {
					SPLog("Exception while processing network packets (ignored):\n%s", ex.what());
				}
			}

			// Repeated seek preview while a seek key is held.
			// Each repeat tick advances demoSeekPendingTime and calls SeekPreview() so the
			// HUD updates smoothly.  The world-replay Seek() is deferred to key release.
			if (demoNet && (demoSeekForwardHeld || demoSeekBackwardHeld)) {
				constexpr float kSeekStep = 5.0F;
				constexpr float kRepeatInterval = 0.35F;
				demoSeekRepeatTimer += dt;
				while (demoSeekRepeatTimer >= kRepeatInterval) {
					demoSeekRepeatTimer -= kRepeatInterval;
					float prev = demoSeekPendingTime;
					float minTime = demoNet->GetBootstrapEndTime();
					float maxTime = demoNet->GetDuration();
					if (demoSeekForwardHeld) {
						demoSeekPendingTime = std::min(maxTime, demoSeekPendingTime + kSeekStep);
						if (demoSeekPendingTime >= maxTime)
							demoSeekForwardHeld = false;
					} else if (demoSeekBackwardHeld) {
						demoSeekPendingTime = std::max(minTime, demoSeekPendingTime - kSeekStep);
						if (demoSeekPendingTime <= minTime)
							demoSeekBackwardHeld = false;
					}
					if (demoSeekPendingTime != prev)
						demoNet->SeekPreview(demoSeekPendingTime);
				}
			}

			hurtRingView->Update(dt);
			centerMessageView->Update(dt);
			mapView->Update(dt);
			largeMapView->Update(dt);
			paletteView->Update(dt);

			// Close the pie menu if the conditions that let it open no longer hold
			// (player died, changed team to spectator, entered limbo, opened scripted UI).
			if (pieMenuView->IsOpen()) {
				bool shouldClose = true;
				if (world && !scriptedUI->NeedsInput() && !inGameLimbo && !staffSpectating) {
					auto maybePlayer = world->GetLocalPlayer();
					if (maybePlayer) {
						Player& lp = maybePlayer.value();
						if (lp.IsAlive() && !lp.IsSpectator())
							shouldClose = false;
					}
				}
				if (shouldClose)
					pieMenuView->Close();
			}
			pieMenuView->Update(dt);

			UpdateDamageIndicators(dt);
			UpdateAutoFocus(dt);

			if (world) {
				float gameplayDt = demoNet ? dt * demoNet->GetSpeed() : dt;
				UpdateWorld(dt, gameplayDt);
				mumbleLink.Update(world->GetLocalPlayer().get_pointer());
			} else {
				renderer->SetFogColor(MakeVector3(0, 0, 0));
			}

			// Auto-follow a player for menuless demo replay, before the scene is
			// composed so the follow camera takes effect on the rendered frame.
			if (demoReplayFollowPending)
				UpdateDemoReplayFollow();

			chatWindow->Update(dt);
			killfeedWindow->Update(dt);
			limbo->Update(dt);

			// The loading screen
			NetClientStatus currentStatus = activeNet->GetStatus();
			if (currentStatus == NetClientStatusReceivingMap) {
				// Apply temporal smoothing on the progress value
				float progress = activeNet->GetMapReceivingProgress();

				if (mapReceivingProgressSmoothed > progress)
					mapReceivingProgressSmoothed = progress;
				else
					mapReceivingProgressSmoothed =
					  Mix(mapReceivingProgressSmoothed, progress, 1.0F - powf(0.05F, dt));
			} else {
				mapReceivingProgressSmoothed = 0.0F;
			}

			// CreateSceneDefinition also can be used for sounds
			SceneDefinition sceneDef = CreateSceneDefinition();
			lastSceneDef = sceneDef;
			UpdateMatrices();

			// Update sounds
			try {
				audioDevice->Respatialize(sceneDef.viewOrigin,
					sceneDef.viewAxis[2], sceneDef.viewAxis[1]);
			} catch (const std::exception& ex) {
				SPLog("Audio subsystem returned error (ignored):\n%s", ex.what());
			}

			// render scene
			DrawScene();

			// draw 2d
			Draw2D();

			// draw scripted GUI
			scriptedUI->RunFrame(dt);
			if (scriptedUI->WantsClientToBeClosed())
				readyToClose = true;

			time += dt;
		}

		void Client::RunFrameLate(float dt) {
			SPADES_MARK_FUNCTION();

			// Well done!
			renderer->FrameDone();
			renderer->Flip();
		}

		void Client::EnableDemoReplayFollow(const std::string& playerSpec) {
			demoReplayFollowPending = true;
			demoReplayFollowSpec = playerSpec;
		}

		int Client::ResolveDemoPlayer(const std::string& spec) {
			if (!world)
				return -1;

			size_t slots = world->GetNumPlayerSlots();

			if (!spec.empty()) {
				// A purely numeric spec is treated as a player id (slot index).
				bool numeric = true;
				for (char c : spec) {
					if (c < '0' || c > '9') {
						numeric = false;
						break;
					}
				}
				if (numeric) {
					int id = std::atoi(spec.c_str());
					if (id >= 0 && (size_t)id < slots && world->GetPlayer((unsigned int)id))
						return id;
					SPLog("Demo: player id %s not present, using first player", spec.c_str());
				} else {
					for (size_t i = 0; i < slots; i++) {
						auto p = world->GetPlayer((unsigned int)i);
						if (p && p->GetName() == spec)
							return (int)i;
					}
					SPLog("Demo: player named '%s' not found, using first player", spec.c_str());
				}
			}

			// Default / fallback: first player that is on a team (not a spectator),
			// since spectators have no body to watch.
			for (size_t i = 0; i < slots; i++) {
				auto p = world->GetPlayer((unsigned int)i);
				if (p && !p->IsSpectator())
					return (int)i;
			}

			// All occupied slots are spectators; fall back to the first one.
			for (size_t i = 0; i < slots; i++) {
				if (world->GetPlayer((unsigned int)i))
					return (int)i;
			}
			return -1;
		}

		void Client::UpdateDemoReplayFollow() {
			if (!world || !demoNet || activeNet->GetStatus() != NetClientStatusConnected)
				return;

			// Wait until at least one player exists, then follow it. ResolveDemoPlayer
			// returns -1 while the world has no players, so this stays pending across
			// the first frames of playback without ever blocking.
			int pid = ResolveDemoPlayer(demoReplayFollowSpec);
			if (pid < 0)
				return;

			followedPlayerId = pid;
			followCameraState.enabled = true;
			followCameraState.firstPerson = true;
			demoReplayFollowPending = false;
		}

		bool Client::HasLocalPlayer() {
			return world && world->GetLocalPlayer();
		}

		bool Client::IsLimboViewActive() {
			// In demo mode, never show limbo view - user is spectating
			if (IsDemoMode())
				return false;
			return world && (!world->GetLocalPlayer() || inGameLimbo);
		}

		void Client::CloseLimboView() {
			if (!IsLimboViewActive())
				return;
			inGameLimbo = false;
		}

		void Client::SpawnPressed() {
			// In demo mode, player actions are replayed from the demo file
			if (IsDemoMode())
				return;
			
			WeaponType weap = limbo->GetSelectedWeapon();
			int team = limbo->GetSelectedTeam();
			if (team == 2)
				team = 255;

			stmp::optional<Player&> maybePlayer = world->GetLocalPlayer();
			if (!maybePlayer || maybePlayer->IsSpectator()) { // join
				if (team == 255) {
					// weaponId doesn't matter for spectators, but
					// NetClient doesn't like invalid weapon ID
					weap = WeaponType::RIFLE_WEAPON;
				}
				activeNet->SendJoin(team, weap, playerName, lastScore);
			} else { // localplayer has joined
				Player& p = maybePlayer.value();

				const auto curTeam = p.GetTeamId();
				const auto curWeap = p.GetWeapon().GetWeaponType();

				if (team != curTeam)
					activeNet->SendTeamChange(team);
				if (team != 255 && weap != curWeap)
					activeNet->SendWeaponChange(weap);
			}

			// set loadout
			limbo->SetSelectedTeam(team);
			limbo->SetSelectedWeapon(weap);
			inGameLimbo = false;
		}

		void Client::ShowAlert(const std::string& contents, AlertType type) {
			if (!cg_alerts) {
				chatWindow->AddMessage(ChatWindow::ColoredMessage(contents, MsgColorRed));
				if (type != AlertType::Notice)
					PlayAlertSound();
				return;
			}

			float timeout;
			switch (type) {
				case AlertType::Notice: timeout = 2.5F; break;
				case AlertType::Warning:
				case AlertType::Error: timeout = 3.0F; break;
				default: timeout = 0.0F; break;
			}
			ShowAlert(contents, type, timeout);
		}

		void Client::ShowAlert(const std::string& contents, AlertType type, float timeout, bool quiet) {
			alertType = type;
			alertContents = contents;
			alertDisappearTime = time + timeout;
			alertAppearTime = time;

			if (type != AlertType::Notice && !quiet)
				PlayAlertSound();
		}

		void Client::PlayAlertSound() {
			Handle<IAudioChunk> c = audioDevice->RegisterSound("Sounds/Feedback/Alert.opus");
			AudioParam params;
			params.volume = (float)cg_alertSounds;
			audioDevice->PlayLocal(c.GetPointerOrNull(), params);
		}

		void Client::PlayScreenshotSound() {
			Handle<IAudioChunk> c = audioDevice->RegisterSound("Sounds/Misc/SwitchMapZoom.opus");
			audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
		}

		void Client::LoadKillSounds() {
			// list available sounds in the directory
			auto files = FileManager::EnumFiles("Sounds/Feedback/Killstreak");
			for (const auto& file : files) {
				// check extension
				if (file.size() < 5 || file.rfind(".opus") != file.size() - 5)
					continue;

				auto fullPath = "Sounds/Feedback/Killstreak/" + file;

				// register sound
				killSounds.push_back(audioDevice->RegisterSound(fullPath.c_str()));
			}

			int soundsNum = static_cast<int>(killSounds.size());
			if (soundsNum > 0)
				SPLog("Loaded %d killstreak sounds", soundsNum);
		}

		/** Records chat message/game events to the log file. */
		void Client::NetLog(const char* format, ...) {
			char buf[4096];
			va_list va;
			va_start(va, format);
			vsnprintf(buf, sizeof(buf), format, va);
			va_end(va);
			std::string str = buf;

			time_t t;
			struct tm tm;
			::time(&t);
			tm = *localtime(&t);

			char timeBuf[32];
			strftime(timeBuf, sizeof(timeBuf), "%a %b %d %H:%M:%S %Y", &tm);
			std::string timeStr = timeBuf;

			snprintf(buf, sizeof(buf), "%s %s\n", timeStr.c_str(), str.c_str());
			buf[sizeof(buf) - 1] = 0;

			std::string outStr = EscapeControlCharacters(buf);
			printf("%s", outStr.c_str());

			if (logStream)
				logStream->Write(outStr);
		}

#pragma mark - Snapshots

		void Client::TakeMapShot() {
			try {
				std::string name = MapShotPath();
				{
					auto stream = FileManager::OpenForWriting(name.c_str());
					try {
						const Handle<GameMap>& map = GetWorld()->GetMap();
						if (!map)
							SPRaise("No map loaded");
						map->Save(stream.get());
					} catch (...) {
						throw;
					}
				}

				std::string msg = _Tr("Client", "Map saved: {0}", name);
				ShowAlert(msg, AlertType::Notice);
				SPLog("Map saved: %s", name.c_str());
			} catch (const Exception& ex) {
				std::string msg = _Tr("Client", "Saving map failed: ");
				msg += ex.GetShortMessage();
				ShowAlert(msg, AlertType::Error);
				SPLog("Saving map failed: %s", ex.what());
			} catch (const std::exception& ex) {
				std::string msg = _Tr("Client", "Saving map failed: ");
				msg += ex.what();
				ShowAlert(msg, AlertType::Error);
				SPLog("Saving map failed: %s", ex.what());
			}
		}

		std::string Client::MapShotPath() {
			char buf[32];
			const int maxShotIndex = 10000;
			for (int i = 0; i < maxShotIndex; i++) {
				snprintf(buf, sizeof(buf), "Mapshots/shot%04d.vxl", nextMapShotIndex);
				if (FileManager::FileExists(buf)) {
					nextMapShotIndex++;
					if (nextMapShotIndex >= maxShotIndex)
						nextMapShotIndex = 0;
					continue;
				}

				return buf;
			}

			SPRaise("No free file name");
		}

#pragma mark - Chat Messages

		void Client::PlayerSentChatMessage(Player& p, bool global, const std::string& msg) {
			std::string msgType = global
				? _Tr("Client", "Global") : _Tr("Client", "Team");
			std::string playerNameStr = p.GetName();
			std::string teamNameStr = p.IsSpectator()
				? _Tr("Client", "Spectator") : p.GetTeamName();
			NetLog("[%s] %s (%s): %s",
				msgType.c_str(), playerNameStr.c_str(),
				teamNameStr.c_str(), msg.c_str());

			std::string state = "";
			if (!p.IsAlive()) {
				state = _Tr("Client", "(DEAD)");
			} else if (p.IsSpectator()) {
				state = _Tr("Client", "(SPEC)");
			}

			{
				std::string s;

				// add prefix
				if (global) {
					s += _Tr("Client", "[Global]");
					s += " ";
				}

				// add player name
				s += playerNameStr;

				// add player state
				if (!state.empty()) {
					s += " ";
					s += state;
				}

				s += ": ";
				s += msg;
				scriptedUI->RecordChatLog(s, ConvertColorRGBA(p.GetColor()));
			}

			// filters chat messages (but they can still be found in logs)
			if (cg_ignoreChatMessages && !p.IsLocalPlayer())
				return;

			{
				std::string s;

				// add prefix
				if (global) {
					s += _Tr("Client", "[Global]");
					s += " ";
				}

				// add player name (team-colored)
				s += ChatWindow::TeamColorMessage(playerNameStr, p.GetTeamId());

				// add player state
				if (!state.empty()) {
					s += " ";
					s += state;
				}

				s += ": ";
				s += msg;
				chatWindow->AddMessage(s);
			}

			if (!IsMuted()) {
				Handle<IAudioChunk> c = audioDevice->RegisterSound("Sounds/Feedback/Chat.opus");
				AudioParam params;
				params.volume = (float)cg_chatBeep;
				audioDevice->PlayLocal(c.GetPointerOrNull(), params);
			}
		}

		void Client::ServerSentMessage(bool system, const std::string& msg) {
			NetLog("%s", msg.c_str());
			scriptedUI->RecordChatLog(msg);

			if (cg_serverAlert) {
				if (msg.substr(0, 3) == CHATPREFIX_BIG) {
					centerMessageView->AddMessage(msg.substr(3));
					return;
				} else if (msg.substr(0, 3) == CHATPREFIX_NOTICE) {
					ShowAlert(msg.substr(3), AlertType::Notice);
					return;
				} else if (msg.substr(0, 3) == CHATPREFIX_WARNING) {
					ShowAlert(msg.substr(3), AlertType::Warning);
					return;
				} else if (msg.substr(0, 3) == CHATPREFIX_ERROR) {
					ShowAlert(msg.substr(3), AlertType::Error);
					return;
				}
			}

			// Display system messages in yellow
			if (system) {
				chatWindow->AddMessage(ChatWindow::ColoredMessage(msg, MsgColorSysInfo));
				return;
			}

			// Display private messages in green
			if (msg.substr(0, 8) == "PM from ") {
				if (cg_ignorePrivateMessages)
					return;

				// play chat sound
				if (!IsMuted()) {
					Handle<IAudioChunk> c = audioDevice->RegisterSound("Sounds/Feedback/Chat.opus");
					AudioParam params;
					params.volume = (float)cg_chatBeep;
					audioDevice->PlayLocal(c.GetPointerOrNull(), params);
				}

				std::string s = _Tr("Client", "[PM] ");
				s += ChatWindow::ColoredMessage(msg.substr(8), MsgColorGreen);
				chatWindow->AddMessage(s);
				return;
			}

			chatWindow->AddMessage(msg);
		}

#pragma mark - Follow / Spectate

		void Client::FollowNextPlayer(bool reverse) {
			stmp::optional<Player&> maybePlayer = world->GetLocalPlayer();

			// In demo mode, there's no local player - we're always spectating
			bool localPlayerIsSpectating = true;
			bool skipDeadPlayers = false;
			int localPlayerId = -1;

			if (maybePlayer) {
				Player& localPlayer = maybePlayer.value();
				bool localPlayerIsSpectador = localPlayer.IsSpectator();
				localPlayerIsSpectating = localPlayerIsSpectador || staffSpectating;
				skipDeadPlayers = !localPlayerIsSpectador && cg_skipDeadPlayersWhenDead;
				localPlayerId = localPlayer.GetId();
			}

			int nextId = FollowsNonLocalPlayer(GetCameraMode())
				? followedPlayerId : localPlayerId;

			auto slots = world->GetNumPlayerSlots();

			// Ensure nextId is valid
			if (nextId < 0 || nextId >= static_cast<int>(slots))
				nextId = 0;

			int startId = nextId;
			do {
				reverse ? --nextId : ++nextId;

				if (nextId >= static_cast<int>(slots))
					nextId = 0;
				if (nextId < 0)
					nextId = static_cast<int>(slots - 1);

				stmp::optional<Player&> p = world->GetPlayer(nextId);
				if (!p || p->IsSpectator())
					continue; // Do not follow a non-existent player or spectator
				if (!localPlayerIsSpectating && maybePlayer && !p->IsTeammate(maybePlayer.value()))
					continue; // Skip enemies unless the local player is a spectator
				if (skipDeadPlayers && !p->IsAlive())
					continue; // Skip dead players if the local player is not a spectator

				// Do not follow a player with an invalid state
				if (p->GetFront().GetSquaredLength() < 0.01F)
					continue;

				break;
			} while (nextId != startId);

			followedPlayerId = nextId;
			followCameraState.enabled = IsDemoMode() || staffSpectating || (followedPlayerId != localPlayerId);
		}
	} // namespace client
} // namespace spades