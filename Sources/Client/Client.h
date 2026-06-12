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

#include <list>
#include <array>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>

#include "ClientCameraMode.h"
#include "DemoNetClient.h"
#include "INetClient.h"
#include "NetClient.h"
#include "ILocalEntity.h"
#include "IRenderer.h"
#include "IWorldListener.h"
#include "MumbleLink.h"
#include "NoiseSampler.h"
#include "Player.h"
#include <Core/Math.h>
#include <Core/ServerAddress.h>
#include <Core/Stopwatch.h>
#include <Core/VersionInfo.h>
#include <Gui/View.h>

namespace spades {
	class IStream;
	class Stopwatch;
	namespace client {
		class IRenderer;
		struct SceneDefinition;
		class GameMap;
		class GameMapWrapper;
		class World;
		struct PlayerInput;
		struct WeaponInput;
		class IAudioDevice;
		class IAudioChunk;
		class NetClient;
		class IFont;
		class FontManager;
		class ChatWindow;
		class CenterMessageView;
		class Corpse;
		class HurtRingView;
		class MapView;
		class ScoreboardView;
		class LimboView;
		class Player;
		class PaletteView;
		class TCProgressView;
		class ClientPlayer;
		class BloodMarks;
		class ClientUI;
		class PieMenuView;

		class Client : public IWorldListener, public gui::View {
			friend class ScoreboardView;
			friend class LimboView;
			friend class MapView;
			friend class FallingBlock;
			friend class PaletteView;
			friend class TCProgressView;
			friend class ClientPlayer;
			friend class ClientUI;

			/** used to keep the input state of keypad so that
			 * after user pressed left and right, and then
			 * released right, left is internally pressed. */
			struct KeypadInput {
				bool left, right, forward, backward;
				KeypadInput() : left(false), right(false), forward(false), backward(false) {}
			};

			class FPSCounter {
				Stopwatch sw;
				int numFrames;
				double lastFps;

			public:
				FPSCounter();
				void MarkFrame();
				double GetFps() { return lastFps; }
			};

			FPSCounter fpsCounter;
			FPSCounter upsCounter;

			std::unique_ptr<NetClient> net;
			std::unique_ptr<DemoNetClient> demoNet;
			INetClient* activeNet; // points to net.get() or demoNet.get(), never null after DoInit
			std::string demoFilePath;

			// Seek-key hold state.
			// While a seek key is held, SeekPreview() advances demoSeekPendingTime so
			// the HUD stays responsive.  The full world-replay Seek() fires only on
			// key release, avoiding one expensive reset/replay per repeat tick.
			bool demoSeekForwardHeld = false;
			bool demoSeekBackwardHeld = false;
			float demoSeekRepeatTimer = 0.0F;   // accumulates dt between preview steps
			float demoSeekPendingTime = 0.0F;   // target time to commit on key release
			bool demoHudVisible = true;

			// Automated one-shot follow for menuless demo replay (--replay-demo):
			// once the world has loaded the requested player is followed. Unused
			// unless EnableDemoReplayFollow() has been called.
			bool demoReplayFollowPending = false;
			std::string demoReplayFollowSpec; // empty = first player; else id or name

			std::string playerName;
			std::unique_ptr<IStream> logStream;

			Handle<ClientUI> scriptedUI;

			ServerAddress hostname;

			std::unique_ptr<World> world;
			Handle<GameMap> map;
			std::unique_ptr<GameMapWrapper> mapWrapper;
			Handle<IRenderer> renderer;
			Handle<IAudioDevice> audioDevice;
			float time;
			bool readyToClose;
			float worldSubFrame;
			float worldSubFrameFast;

			int frameToRendererInit;
			float timeSinceInit;

			MumbleLink mumbleLink;

			// view/drawing state for some world objects
			std::vector<Handle<ClientPlayer>> clientPlayers;

			// other windows
			std::unique_ptr<CenterMessageView> centerMessageView;
			std::unique_ptr<HurtRingView> hurtRingView;
			std::unique_ptr<MapView> mapView;
			std::unique_ptr<MapView> largeMapView;
			std::unique_ptr<ScoreboardView> scoreboard;
			std::unique_ptr<LimboView> limbo;
			std::unique_ptr<PaletteView> paletteView;
			std::unique_ptr<TCProgressView> tcView;
			Handle<IImage> debugHitTestImage;

			// chat
			std::unique_ptr<ChatWindow> chatWindow;
			std::unique_ptr<ChatWindow> killfeedWindow;

			// pie menu
			std::unique_ptr<PieMenuView> pieMenuView;

			// player state
			PlayerInput playerInput;
			WeaponInput weapInput;
			KeypadInput keypadInput;
			Player::ToolType lastTool;
			bool hasLastTool;
			Vector3 lastPosSent;
			Vector3 lastFrontSent;
			float lastPosSentTime;
			float lastOriSentTime;
			float lastHurtTime;
			float lastHealTime;
			float lastAliveTime;
			int lastRespawnCount;
			float lastHitTime;
			int damageTaken;
			int lastHealth;
			int lastScore;
			int curKills;
			int curDeaths;
			int curStreak;
			int bestStreak;
			int meleeKills;
			int grenadeKills;
			int placedBlocks;

			std::unordered_map<int, std::unordered_map<int, int>> killStreaks;

			struct WeaponStats {
				std::array<int, 3> hits, shots;
				WeaponStats() : hits({0, 0, 0}), shots({0, 0, 0}) {}
			};
			WeaponStats weaponStats;

			struct HitStats {
				int numHeadHits, numBodyHits, numLimbHits;
				HitStats() : numHeadHits(0), numBodyHits(0), numLimbHits(0) {}
			};
			HitStats hitStats;

			float worldSetTime;

			bool reloadKeyPressed;

			struct HurtSprite {
				float angle;
				float horzShift;
				float scale;
				float strength;
			};
			std::vector<HurtSprite> hurtSprites;

			struct DamageIndicator {
				int damage;
				int playerId;
				float fade;
				float lastHitTime;
				Vector3 position;
				Vector3 velocity;
				bool crit;
				DamageIndicator() : damage(0), playerId(-1),
					fade(0.0F), lastHitTime(0.0F), crit(false) {}
			};
			std::list<DamageIndicator> damageIndicators;

			float GetSprintState();
			float GetAimDownState();

			/**
			 * Queries whether the local player is allowed to use a tool in this state.
			 *
			 * The following factors are considered by this function:
			 *
			 *	- The player cannot use a tool while / soon after sprinting.
			 *	- The player cannot use a tool while switching tools.
			 *	- The player must exist and be alive to use a tool.
			 *
			 * The following factors also affect whether a tool can actually be used, but they
			 * do not affect the result of this function:
			 *
			 *	- Tool-specific status — e.g., out of ammo, out of block, "cannot build there"
			 *	- Firing rate limit imposed by the tool
			 */
			bool CanLocalPlayerUseTool();
			bool CanLocalPlayerUseWeapon();
			bool CanLocalPlayerReloadWeapon();

			/** Retrieves `ClientPlayer` for the local player, or `{}` if it does not exist. */
			stmp::optional<ClientPlayer&> GetLocalClientPlayer();

			void SetSelectedTool(Player::ToolType, bool quiet = false);

			// view
			SceneDefinition lastSceneDef;
			/** Derived from `lastSceneDef`. */
			Matrix4 lastViewProjectionScreenMatrix;
			float localFireVibrationTime;
			float grenadeVibration;
			float grenadeVibrationSlow;
			bool scoreboardVisible;
			bool hudVisible;
			bool flashlightOn;
			float flashlightOnTime;

			CoherentNoiseSampler1D coherentNoiseSamplers[3];
			void KickCamera(float strength);

			bool isChristmasOn;
			float lastSnowDropTime;

			float hotBarIconState;

			float hitFeedbackIconState;
			bool hitFeedbackFriendly;

			float debugHitTestZoomState;
			bool debugHitTestZoom;

			float spectatorZoomState;
			bool spectatorZoom;

			// manual focus
			float focalLength;
			float targetFocalLength;
			bool autoFocusEnabled;

			// Spectator camera control
			/** The state of the following camera used for spectating. */
			struct {
				/**
				 * Indicates whether the current camera mode is first-person or not.
				 * Ignored and locked to third-person when the target player
				 * (`followedPlayerId`) is dead.
				 */
				bool firstPerson = true;

				/** Controls whether the follow camera is enabled. */
				bool enabled = false;
			} followCameraState;

			/** The state of the free floating camera used for spectating. */
			struct {
				/** The temporally smoothed position (I guess). */
				Vector3 position{0.0F, 0.0F, 0.0F};

				/** The temporally smoothed velocity (I guess). */
				Vector3 velocity{0.0F, 0.0F, 0.0F};
			} freeCameraState;

			/**
			 * The state shared by both of the third-person and free-floating cameras.
			 *
			 * Note: These values are not used in the `cg_thirdperson` mode.
			 */
			struct {
				/** The yaw angle. */
				float yaw = 0.0F;

				/** The pitch angle. */
				float pitch = 0.0F;
			} followAndFreeCameraState;

			/**
			 * The ID of the player being followed (in a spectator mode, or when the local player is
			 * dead). Must be valid as long as the follow cam is enabled.
			 *
			 * Must *not* specify a local player.
			 */
			int followedPlayerId;

			/**
			 * Chooses the next player to follow and assigns it to `this->followingPlayerId`.
			 * Enables the follow cam by assigning `true` to `followCameraState.enabled`.
			 * If the next player is the local player, disables the follow cam.
			 */
			void FollowNextPlayer(bool reverse);

			/**
			 * Retrieves the current camera mode.
			 */
			ClientCameraMode GetCameraMode();

			/**
			 * Retrieves the target player ID of the current camera mode (as returned by
			 * `GetCameraMode`).
			 *
			 * Throws an exception if the current camera mode does not have a player in concern.
			 */
			int GetCameraTargetPlayerId();

			/**
			 * Retrieves the target player of the current camera mode (as returned by
			 * `GetCameraMode`).
			 *
			 * Throws an exception if the current camera mode does not have a player in concern.
			 */
			Player& GetCameraTargetPlayer();

			bool IsInFirstPersonView(int playerId);

			/**
			 * Calculate the zoom value incorporating the effect of ADS for a first-person view.
			 *
			 * The camera mode must be first-person.
			 */
			float GetAimDownZoomScale();
			float GetLocalFireVibration();

			void SetBlockColor(IntVector3 color);
			void CaptureColor();

			bool inGameLimbo;
			bool IsLimboViewActive();
			void CloseLimboView();
			void SpawnPressed();

			stmp::optional<std::tuple<Player&, hitTag_t>> HotTrackedPlayer();

			// effects (local entity, etc)
			std::vector<DynamicLightParam> flashDlights;
			std::vector<DynamicLightParam> flashDlightsOld;
			void Bleed(Vector3, bool bounce = false);
			void EmitBlockFragments(Vector3, IntVector3 color);
			void EmitBlockDestroyFragments(IntVector3 pos);
			void GrenadeExplosion(Vector3);
			void GrenadeExplosionUnderwater(Vector3);
			void MuzzleFire(Vector3, bool smoke = true);
			void BulletHitWaterSurface(Vector3);
			void EmitSnowflakes(Vector3);

			// drawings
			Handle<FontManager> fontManager;

			enum class AlertType { Notice, Warning, Error };
			AlertType alertType;
			std::string alertContents;
			float alertDisappearTime;
			float alertAppearTime;

			// Loading screen
			float mapReceivingProgressSmoothed = 0.0F;

			std::list<std::unique_ptr<ILocalEntity>> localEntities;
			std::list<std::unique_ptr<Corpse>> corpses;
			Corpse* lastLocalCorpse;
			void RemoveCorpses();
			void RemoveInvisibleCorpses();
			void RemoveCorpseForPlayer(int playerId);
			void RemoveAllLocalEntities();

			std::unique_ptr<BloodMarks> bloodMarks;

			int nextScreenShotIndex;
			int nextMapShotIndex;

			/** Project the specified world-space position to a screen space. */
			bool Project(const Vector3&, Vector2&);

			/** Recalculate `lastViewProjectionScreenMatrix` based on the current value of
			 * `lastSceneDef`. */
			void UpdateMatrices();

			void DrawSplash();
			void DrawStartupScreen();
			void DoInit();

			void ShowAlert(const std::string& contents, AlertType type);
			void ShowAlert(const std::string& contents, AlertType type,
				float timeout, bool quiet = false);
			void PlayAlertSound();
			void PlayScreenshotSound();

			// Killsteak sounds
			std::vector<Handle<IAudioChunk>> killSounds;
			void LoadKillSounds();

			void UpdateWorld(float dt, float gameplayDt);
			void UpdateLocalSpectator(float dt);
			void UpdateLocalPlayer(float dt);
			void UpdateAutoFocus(float dt);
			float RayCastForAutoFocus(const Vector3& origin, const Vector3& direction);

			void Draw2D();
			void Draw2DWithoutWorld();
			void Draw2DWithWorld();

			/** Called when the local player is alive. */
			void DrawJoinedAlivePlayerHUD();

			/** Called when the local player is dead. */
			void DrawDeadPlayerHUD();

			/**
			 * Called when `IsFirstPerson(GetCameraMode()).` Renders the follwing element:
			 *	- The center reticule
			 */
			void DrawFirstPersonHUD();

			/**
			 * Called when the local player is dead or a spectator.
			 */
			void DrawSpectateHUD();

			bool staffSpectating;
			bool spectatorPlayerNames;

			Vector4 GetPlayerColor(Player&);
			void DrawPlayerBox(Player&, const Vector4&);
			void DrawPlayerName(Player&, const Vector4&);
			void DrawHottrackedPlayerName();
			void DrawPubOVL();
			void DrawDeadPlayers();

			void DrawBlockPaletteHUD(float y);
			void DrawAlivePlayersCount();
			void DrawPlayingTime();
			void DrawRecordingIndicator();
			void DrawDemoPlaybackHUD();
			void DrawHurtSprites();
			void DrawScreenEffect(bool hurt, float fadeTime = 0.35F);
			void DrawAlert();
			void DrawDebugAim(Player&);
			void DrawStats();
			void DrawHitTestDebugger();
			void DrawPlayerStats();

			void UpdateDamageIndicators(float dt);
			void DrawDamageIndicators();

			void DrawScene();
			void AddGrenadeToScene(Grenade&);
			void AddDebugObjectToScene(const OBB3&, const Vector4& col = MakeVector4(1, 1, 1, 1));
			void AddMapObjectsToScene();

			SceneDefinition CreateSceneDefinition();

			std::string ScreenShotPath();
			void TakeScreenShot(bool sceneOnly, bool scoreboardOnly = false);

			// Demo helpers (see EnableDemoReplayFollow).
			void UpdateDemoReplayFollow();
			int ResolveDemoPlayer(const std::string& spec);

			std::string BuildDemoContext();

			std::string MapShotPath();
			void TakeMapShot();

			void NetLog(const char* format, ...);

		protected:
			~Client();

		public:
			Client(Handle<IRenderer>, Handle<IAudioDevice>,
				const ServerAddress& host, Handle<FontManager>,
				const std::string& demoPath = "");

			bool HasLocalPlayer();
			bool IsDemoMode() const { return demoNet != nullptr; }
			DemoNetClient* GetDemoNetClient() { return demoNet.get(); }
			void ReloadDemo();

			/**
			 * Auto-follows a player once the demo world has loaded (empty = first
			 * player; otherwise a player id or name) and keeps normal playback
			 * running. Used by menuless demo replay (--replay-demo). Only
			 * meaningful when the client was created with a demo path.
			 */
			void EnableDemoReplayFollow(const std::string& playerSpec);

			void RunFrame(float dt) override;
			void RunFrameLate(float dt) override;

			void Closing() override;
			void MouseEvent(float x, float y) override;
			void WheelEvent(float x, float y) override;
			void KeyEvent(const std::string&, bool down) override;
			void TextInputEvent(const std::string&) override;
			void TextEditingEvent(const std::string&, int start, int len) override;
			bool AcceptsTextInput() override;
			AABB2 GetTextInputRect() override;
			bool NeedsAbsoluteMouseCoordinate() override;
			bool ExecCommand(const Handle<gui::ConsoleCommand>&) override;
			Handle<gui::ConsoleCommandCandidateIterator>
			AutocompleteCommandName(const std::string& name) override;

			void SetWorld(World*);
			World* GetWorld() const { return world.get(); }

			// Snapshot of all view-related state, used to preserve camera across demo seeks
			struct ViewState {
				bool followEnabled;
				bool followFirstPerson;
				int followedPlayerId;
				Vector3 freePosition;
				Vector3 freeVelocity;
				float yaw;
				float pitch;
				float worldSetTime;
			};
			ViewState SaveViewState() const;
			void RestoreViewState(const ViewState&);
			void AddLocalEntity(std::unique_ptr<ILocalEntity>&& ent) {
				localEntities.emplace_back(std::move(ent));
			}

			void MarkWorldUpdate();

			IRenderer& GetRenderer() { return *renderer; }
			SceneDefinition GetLastSceneDef() { return lastSceneDef; }
			IAudioDevice& GetAudioDevice() { return *audioDevice; }

			float GetTime() { return time; }

			bool WantsToBeClosed() override;
			bool IsMuted();
			bool IsScoreboardVisible() { return scoreboardVisible; }

			void PlayerSentChatMessage(Player&, bool global, const std::string&);
			void ServerSentMessage(bool system, const std::string&);

			void PlayerCapturedIntel(Player&);
			void PlayerPickedIntel(Player&);
			void PlayerDropIntel(Player&);
			void TeamCapturedTerritory(int teamId, int territoryId);
			void TeamWon(int);
			void JoinedGame();
			void LocalPlayerCreated();
			void RegisterPlacedBlocks(int c) { placedBlocks += c; };
			void PlayerCreatedBlock(Player&);
			void PlayBlockDestroySound(Vector3);
			void PlayerDestroyedBlockWithWeaponOrTool(IntVector3);
			void PlayerDiggedBlock(IntVector3);
			void GrenadeDestroyedBlock(IntVector3);
			void PlayerLeaving(Player&);
			void PlayerJoinedTeam(Player&);
			void PlayerSpawned(Player&);

			// IWorldListener begin
			void PlayerObjectSet(int) override;
			void PlayerMadeFootstep(Player&) override;
			void PlayerJumped(Player&) override;
			void PlayerLanded(Player&, bool hurt) override;
			void PlayerFiredWeapon(Player&) override;
			void PlayerEjectedBrass(Player&) override;
			void PlayerDryFiredWeapon(Player&) override;
			void PlayerReloadingWeapon(Player&) override;
			void PlayerReloadedWeapon(Player&) override;
			void PlayerChangedTool(Player&) override;
			void PlayerPulledGrenadePin(Player&) override;
			void PlayerThrewGrenade(Player&, stmp::optional<const Grenade&>) override;
			void PlayerMissedSpade(Player&) override;
			void PlayerRestocked(Player&) override;

			/** @deprecated use BulletHitPlayer */
			void PlayerHitBlockWithSpade(Player&, Vector3 hitPos,
				IntVector3 blockPos, IntVector3 normal) override;
			void PlayerKilledPlayer(Player& killer, Player& victim, KillType) override;

			void BulletHitPlayer(Player& hurtPlayer, HitType, Vector3 hitPos, Player& by,
				std::unique_ptr<IBulletHitScanState>& stateCell) override;
			void BulletNearPlayer(Player& by) override;
			void BulletHitBlock(Vector3, IntVector3 blockPos, IntVector3 normal) override;
			void AddBulletTracer(Player& player, Vector3 muzzlePos, Vector3 hitPos) override;
			void GrenadeExploded(const Grenade&) override;
			void GrenadeBounced(const Grenade&) override;
			void GrenadeDroppedIntoWater(const Grenade&) override;

			void BlocksFell(std::vector<IntVector3>) override;

			void LocalPlayerBlockAction(IntVector3, BlockActionType type) override;
			void LocalPlayerCreatedLineBlock(IntVector3, IntVector3) override;
			void LocalPlayerHurt(HurtType type, Vector3 source) override;
			void LocalPlayerBuildError(BuildFailureReason reason) override;
			// IWorldListener end
		};
	} // namespace client
} // namespace spades