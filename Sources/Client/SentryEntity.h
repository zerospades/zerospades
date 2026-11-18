/*
 Copyright (c) 2013 yvt

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
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "Client.h"
#include "ILocalEntity.h"
#include <Core/Math.h>
#include <vector>

namespace spades {
	namespace client {
		class IModel;
		class IAudioDevice;
		class IAudioChunk;
		class Player;

		// Visual deployable sentry gun, spawned by the "/sentry"
		// chat command on the block the local player is aiming
		// at. Comes in three tiers (the future upgrade mechanic):
		//   1 = classic MG ("Models/Turrets/MG1/"), ROF = the base
		//       cg_sentryFireInterval (default 0.50 s)
		//   2 = faster MG   ("Models/Turrets/MG2/"), ROF = base x 2/3
		//       (default ~0.33 s)
		//   3 = minigun     ("Models/Turrets/MG3/"), ROF = base / 2
		//       (default 0.25 s), with SPINNING barrels (a separate
		//       TurretMGBarrels.kv6) and first-person-minigun-style
		//       bullet smoke trails
		// All three levels load their assets from their own folder
		// (same file names inside); missing MG2/MG3 assets leave the
		// sentry invisible but fully functional (silent shots, no
		// casings), with NO fallback to the lower tiers' assets.
		//
		// Purely cosmetic and LOCAL ONLY: no server traffic, no
		// damage, visible only to the local player (same scope as the
		// other client-side local entities). Any number of sentries
		// can be deployed and active at the same time.
		//
		// SERVER BLOCK TURRETS: the piqueserver script "sentry.py"
		// builds REAL turrets out of two stacked map blocks, colored
		// per tier (the block color doubles as the tier marker).
		// NetClient forwards every block the server builds with its
		// reserved player id to OnServerBlockTurret, which spawns
		// this same animated model AND hides the pillar blocks for
		// the renderer (GameMap::SetHidden: they stay solid —
		// hitbox, raycasts, destructible — but are not drawn, so
		// the model is all the player sees). Still purely visual
		// on the client: the server owns the damage and the kill
		// credit, and the entity dies as soon as its blocks are
		// destroyed (it watches them in Update; no MaxAge for
		// server turrets, the server owns their lifetime). Gated
		// by cg_sentryServer.
		// sentry.py also runs a "heartbeat": every couple of
		// seconds it re-builds the TOP pillar block in green
		// (idle) or red (target locked), so even vanilla
		// clients see the turret state. These heartbeat
		// packets reach OnServerBlockTurret too, which uses
		// them to discover turrets it never saw being placed,
		// e.g. baked into the map of a late joiner (the joiner
		// sees the model appear on its own, within one
		// heartbeat of connecting). While LOCKED the beat
		// carries the server's actual aim (g = yaw, b = pitch
		// packed in the color) and every real shot is announced
		// by a tiny covert SetColor packet (reserved id 34,
		// decoded by NetClient): the model TURNS AND FIRES WHERE
		// THE SERVER REALLY DOES. In server block mode the
		// entity therefore never targets on its own (also fixes
		// enemy turrets, which used to aim at the wrong side
		// since the client cannot know the owner's team); an
		// old pre-v1.5 script sends a plain red beat instead =
		// locked but with no direction, so the model just
		// stands level and silent.
		//
		// Per-frame behavior (Update):
		//   - scans enemy players (not teammates, not spectators, not
		//     the local player) within cg_sentryRange, keeping only
		//     targets with a CLEAR LINE OF SIGHT from the head: map
		//     blocks occlude, enemies behind walls are ignored
		//   - smoothly turns AND pitches the turret head toward the
		//     nearest one (it can aim at players below or above
		//     itself; set cg_sentryAimPitch to 0 to keep the gun
		//     level)
		//   - once the target is inside the aim cone (yaw AND
		//     pitch), fires visual tracers (the engine's own Tracer
		//     entity) at cg_sentryFireInterval, with a 2D rifle shot
		//     played with a manual distance falloff (the fork's
		//     spatial path muffles sounds emitted at block surfaces)
		//
		// The placement derives from the models' bounding boxes:
		// the base sits with its FEET on the deployment point, and
		// the head sits at the top of the base's canvas box. The
		// bounding boxes are the FULL kv6 canvas boxes (not
		// voxel-exact, see GLOptimizedVoxelModel.cpp), so
		// cg_sentryHeadZ is the one-time manual seat offset (world
		// blocks, negative = lower) that puts the head on the
		// actual mount. The auto part scales with cg_sentryScale;
		// the manual offset is absolute.
		class SentryEntity : public ILocalEntity {
			Client &client;
			IAudioDevice *audioDevice;      // may be null when muted
			int level;                     // tier 1-3 (clamped)

			// four fire samples (like the SMG): a random one is
			// picked for each shot. Missing files stay silent.
			static constexpr int FireSampleCount = 4;
			Handle<IAudioChunk> fireChunks[FireSampleCount];

			Handle<IModel> baseModel; // Models/Turrets/MG%d/TurretMGBase.kv6
			Handle<IModel> headModel; // Models/Turrets/MG%d/TurretMG.kv6

			// level 3 only: the minigun's spinning barrel
			// assembly, a SEPARATE model (the level-3 head is
			// the body WITHOUT the barrels), rolled around the
			// model's forward axis in Render3D
			Handle<IModel> barrelsModel; // Models/Turrets/MG3/TurretMGBarrels.kv6

			// ejected shell casing: custom model + vanilla SMG
			// drop/water sounds; a missing model = no casing
			Handle<IModel> casingModel; // Models/Turrets/MG%d/Casing.kv6
			Handle<IAudioChunk> casingDropSound1;
			Handle<IAudioChunk> casingDropSound2;
			Handle<IAudioChunk> casingWaterSound;
			// v28: level-3 barrel spin tick (custom asset in
			// Sounds/Turrets/MG3/; may stay null = silent spin)
			Handle<IAudioChunk> spinTickSound;

			Vector3 position; // deployment point (aimed block surface)
			int myTeamId;     // team filter (initial: deploy-time team; refreshed every scan)

			// ---- server block turrets (see OnServerBlockTurret) ----
			// spawned from server-placed blocks instead of the
			// local "/sentrylocal" deploy; dies when its blocks do
			bool serverBlockMode = false;
			// the LOWER pillar block placed by the server script;
			// the anchor (the model seats on its top face, and
			// the TOP pillar block sits at z - 1)
			IntVector3 serverBlock;

			// ---- server aim sync (v44) ----
			// the locked heartbeat colors carry the server's
			// yaw/pitch (decoded by OnServerBlockTurret); the
			// pending flag hands a freshly received aim from the
			// packet path to Update, which lerps the head toward
			// it while fresh and stands down once it goes stale
			// (idle beat = no more data)
			bool serverAimPending = false;
			float serverAimYaw = 0.f;
			float serverAimPitch = 0.f;
			float serverAimTime = -1000.f; // entity age of the last aim
			// seconds without aim data = considered idle again
			static constexpr float ServerAimTimeout = 0.75f;

			// all live server block turrets (dedup on placement;
			// entries remove themselves in the destructor)
			static std::vector<SentryEntity *> s_serverTurrets;

			float currentYaw = 0.f;   // smooth head yaw (radians, 0 = +Y)
			float currentPitch = 0.f; // smooth gun pitch (positive = below horizon)
			bool hasTarget = false;
			float fireTimer = 0.f;  // seconds until the next shot
			float flashTimer = 0.f; // muzzle flash decay
			float age = 0.f;

			// level 3 only: minigun barrel roll state. Each shot
			// kicks the spin up (in Fire, clamped at BarrelSpinMax);
			// Update decelerates it continuously once the firing
			// stops, so the barrels wind up while firing and wind
			// down after, like the first-person minigun skin
			float barrelsRoll = 0.f;    // current roll angle, radians
			float barrelsSpinVel = 0.f; // current roll speed, rad/s
			// v28: countdown to the next spin tick (2 ticks per
			// revolution, like the first-person minigun skin)
			float spinTickTimer = 0.f;

			// cg_sentryDebug log throttle, PER INSTANCE: several
			// sentries can coexist (one per "/sentry" call) and
			// each logs its own scan verdicts, one line per second
			float nextDebugLog = 0.f;

			// ---- Tunables (see SentryEntity.cpp for the live-tunable
			//      settings; primary stats = cg_sentryRange,
			//      cg_sentryFireInterval) --------------------------------
			static constexpr float TurnSpeed = 4.0f; // head, radians/s
			static constexpr float AimConeDeg = 10.f; // fire cone half-angle
			// gun pitch clamp, degrees off horizontal (the turret
			// can aim at players below or above itself)
			static constexpr float MaxAimPitchDeg = 80.f;
			// bullet velocity fed to the engine's Tracer entity; the
			// engine uses 700 (rifle), 360 (SMG), 550 (shotgun)
			static constexpr float BulletVel = 500.f;
			// muzzle flash light lifetime (s)
			static constexpr float FlashTime = 0.08f;
			// ray-step budget for the line-of-sight target check
			// (same as the engine's own weapon raycast)
			static constexpr int LOSRaySteps = 256;
			// hard safety cap (s); all local entities also die with
			// the map anyway (they are cleared on game leave)
			static constexpr float MaxAge = 3600.f;
			// level-3 minigun barrel spin (same values as the
			// first-person M134 minigun skin)
			static constexpr float BarrelSpinMax = 25.f;  // rad/s
			static constexpr float BarrelSpinAccel = 6.f; // spin kick per shot, rad/s
			static constexpr float BarrelSpinDown = 8.f;  // wind-down, rad/s^2
			// roll wrap threshold, full turns: keeps the roll
			// angle small so hours of spinning never lose float
			// precision (same idea as the minigun skin's wrap)
			static constexpr float BarrelRollWrapTurns = 100.f;
			// v28: barrel spin tick profile (same values as the
			// first-person M134 minigun skin)
			static constexpr float SpinTickVolume = 0.08f;        // relative to the shot volume
			static constexpr float SpinTickMinSpeed = 1.f;       // rad/s; below: no ticking
			static constexpr float SpinTicksPerRevolution = 2.f; // ticks per barrel revolution

		public:
			/** serverBlock: the LOWER pillar block placed by
			 *  the server script, i.e. the anchor (negative x =
			 *  none, plain local deploy, e.g. "/sentrylocal";
			 *  the model seats on its top face). */
			SentryEntity(Client &client, IAudioDevice *audioDevice,
			             Vector3 deployPos, int level = 1,
			             IntVector3 serverBlock = IntVector3(-1, 0, 0));
			~SentryEntity();

			/** Maps a server block color to a turret tier (1-3),
			 *  0 when the color is not a sentry tier color. */
			static int LevelFromServerColor(IntVector3 color);
			/** Called by NetClient for every block the SERVER builds
			 *  (reserved player id): when the color matches a sentry
			 *  tier, renders an animated turret over the blocks.
			 *  Gated by cg_sentryServer. */
			static void OnServerBlockTurret(Client &client,
			                                 IAudioDevice *audioDevice,
			                                 IntVector3 blockPos,
			                                 IntVector3 color);
			/** Called by NetClient's SetColor hook for the
			 *  reserved id sentry.py announces each REAL shot
			 *  on: packedPos packs the turret's (x, y) anchor
			 *  column; plays the visual shot (tracer, flash,
			 *  sound) at the exact moment the server-side
			 *  damage lands. */
			static void OnServerSentryFire(int packedPos);

			/** @return false when the entity should be removed. */
			bool Update(float dt) override;
			void Render3D() override;

		private:
			/** Shared spawn path for server block turrets:
			 *  dedup, hide both pillar blocks, seat the model
			 *  on the pillar top. lowerBlock = the LOWER pillar
			 *  block. @return false when a turret is already
			 *  spawned there. */
			static bool SpawnServerTurret(Client &client,
			                               IAudioDevice *audioDevice,
			                               IntVector3 lowerBlock,
			                               int tier);
			/** Steer the server-block turret anchored at (x, y)
			 *  toward the server's current aim (decoded from a
			 *  locked heartbeat color). */
			static void SetServerAim(int x, int y, float yaw,
			                         float pitch);
			/** Visual shot along the server's last aim, for a
			 *  real server-side shot (OnServerSentryFire). */
			void ServerFire();
			/** Shared visual shot (tracer, flash, sound, casing,
			 *  barrel kick) along the given direction; used by
			 *  the local Fire and by ServerFire. */
			void FireVisual(Vector3 aimDir);
			/** Nearest enemy in range AND visible from the head. */
			Player *FindTarget();
			/** Per-tier fire interval: the ONE console-tunable base
			 *  (cg_sentryFireInterval) scaled per level, so live
			 *  calibration shifts the whole ladder proportionally
			 *  (defaults: 1 = base, 2 = base x 2/3, 3 = base / 2). */
			float FireIntervalSeconds() const;
			/** True when no solid block stands between the two
			 *  points. Players never block sight: map blocks only
			 *  (the same rule as the engine's tracer visuals). */
			bool HasLineOfSight(Vector3 from, Vector3 to) const;
			Vector3 HeadOrigin() const;
			Vector3 HeadForward() const; // horizontal, yaw only
			/** Aim direction including the gun pitch. */
			Vector3 AimForward() const;
			void Fire(Player &target);
		};
	} // namespace client
} // namespace spades