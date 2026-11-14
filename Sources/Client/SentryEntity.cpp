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

#include "SentryEntity.h"
#include "IRenderer.h"
#include "IModel.h"
#include "IImage.h"
#include "Client.h"
#include "World.h"
#include "GameMap.h"
#include "Player.h"
#include "Tracer.h"
#include "ParticleSpriteEntity.h"
#include "IAudioDevice.h"
#include "IAudioChunk.h"
#include <Core/Settings.h>
#include <Core/Debug.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// ============================ SENTRY CONFIG ============================
// All of these are live-tunable in the in-game console (change the
// value, test, then freeze the winning values here). The DEFINE of
// each item lives ONLY in this file: a duplicate DEFINE anywhere else
// in the project is a linker error. cg_sentry itself (the "/sentry"
// command gate) is DEFINE'd in ClientUI.cpp.
//
// --- Primary stats (the values the turret tiers scale from) ---
DEFINE_SPADES_SETTING(cg_sentryRange, "48");          // targeting range, blocks
// fire interval BASE: the per-tier rate derives from it
// (level 1 = base, level 2 = base x 2/3, level 3 = base / 2;
// defaults 0.50 / ~0.33 / 0.25 s), so console calibration
// shifts the whole tier ladder proportionally
DEFINE_SPADES_SETTING(cg_sentryFireInterval, "0.50"); // base seconds between shots
DEFINE_SPADES_SETTING(cg_sentryAimPitch, "1");        // 0 = keep the gun level, 1 = tilt it at the target
DEFINE_SPADES_SETTING(cg_sentryAimBody, "0.5");       // aim this far BELOW the target's eye, blocks (z-down; 0 = the old eye-level aim)
DEFINE_SPADES_SETTING(cg_sentryVolume, "1");          // shot volume multiplier
DEFINE_SPADES_SETTING(cg_sentryDebug, "0");          // 1 = log target scan verdicts once per second
DEFINE_SPADES_SETTING(cg_sentryServer, "1");        // 1 = render the animated model over server block turrets (see OnServerBlockTurret)
//
// --- Placement / visuals ---
DEFINE_SPADES_SETTING(cg_sentryScale, "0.1");  // model render scale (calibrate to the kv6)
DEFINE_SPADES_SETTING(cg_sentryHeadZ, "-0.1");  // manual seat offset, world blocks (negative = lower); calibrated in game, recalibrate after any scale change
DEFINE_SPADES_SETTING(cg_sentry1Muzzle, "0.78"); // level-1 muzzle distance from the head pivot (frozen: calibrated in game, 14 Nov)
DEFINE_SPADES_SETTING(cg_sentry2Muzzle, "1.5"); // level-2 muzzle distance from the head pivot (frozen: calibrated in game, 14 Nov)
DEFINE_SPADES_SETTING(cg_sentry3Muzzle, "1.43"); // level-3 muzzle distance from the head pivot (frozen: calibrated in game, 14 Nov)
DEFINE_SPADES_SETTING(cg_sentry3MuzzleZ, "0.43"); // level-3 muzzle vertical trim, world blocks (positive = up; frozen: calibrated in game, 14 Nov)
DEFINE_SPADES_SETTING(cg_sentryFlash, "1");     // muzzle flash dynamic light (0 = shots fire with no light: live bisect switch, see Render3D)
DEFINE_SPADES_SETTING(cg_sentryYawFix, "0");   // constant orientation fixes, in the
DEFINE_SPADES_SETTING(cg_sentryPitchFix, "0"); // MODEL's local frame, degrees, applied
DEFINE_SPADES_SETTING(cg_sentryRollFix, "0");  // to BOTH models
//
// --- Level-3 minigun barrels ---
// Offset from the kv6 pivot to the TRUE barrel axis, in MODEL
// units (the spin axis is Y, so only x and z matter; y cancels
// out). The spin is applied as T(offset) * R * T(-offset), so
// the barrels rotate around the true axis whatever the kv6's
// pivot is — same trick as the first-person minigun skin,
// whose values these defaults are (that skin reuses the same
// barrel model). Live-tunable: watch the barrels while they
// spin and adjust until they stop wobbling.
DEFINE_SPADES_SETTING(cg_sentryBarrelPivotX, "-0.5");
DEFINE_SPADES_SETTING(cg_sentryBarrelPivotZ, "-0.5");
//
// --- Level-3 minigun part tuning ---
// The MG3 body (the level-3 head model) and the spinning barrels
// render at their OWN scale multipliers of cg_sentryScale
// (calibrated live in the console, then frozen here): the body
// 25% smaller, the barrels 35% smaller. The seat offsets are in
// each part's MODEL units (Slab6 units, applied BEFORE the
// part's scale): Y = forward (toward the muzzle), Z = DOWN, so a
// NEGATIVE Z moves a part UP. The barrels ride the body's seat
// first (they are mounted on the body), then their own offset.
DEFINE_SPADES_SETTING(cg_sentry3BodyScale, "0.75");
DEFINE_SPADES_SETTING(cg_sentry3BodyOffX, "-0.01"); // body seat, left = negative
DEFINE_SPADES_SETTING(cg_sentry3BodyOffY, "0.25"); // body seat, forward
DEFINE_SPADES_SETTING(cg_sentry3BodyOffZ, "-0.75"); // body seat, up = negative
DEFINE_SPADES_SETTING(cg_sentry3BarrelScale, "0.65");
DEFINE_SPADES_SETTING(cg_sentry3BarrelOffY, "-0.07"); // barrels seat, forward
DEFINE_SPADES_SETTING(cg_sentry3BarrelOffZ, "0.57"); // barrels seat, up = negative

// --- casings & muzzle-flash tuning (v25) ---
DEFINE_SPADES_SETTING(cg_sentryCasingFwd, "-0.5"); // casing throw along the aim dir, levels 1-2 (negative = behind the gun; live-tunable)
DEFINE_SPADES_SETTING(cg_sentry2CasingScale, "1.25"); // level-2 casing size (1 = level-1 size)
DEFINE_SPADES_SETTING(cg_sentry3CasingScale, "1.5"); // level-3 casing size
DEFINE_SPADES_SETTING(cg_sentry3CasingFwd, "-0.5"); // level-3 casing ejection seat: distance along the aim from the head pivot (frozen: calibrated in game, 14 Nov)
DEFINE_SPADES_SETTING(cg_sentryFlashRadius, "14"); // muzzle light radius, wider than the bullets' tracer light (10)
//
// --- Levels 1-2 muzzle puff tuning (v29: the v25 values were
// near-invisible in game; live-tunable, freeze the winning values) ---
DEFINE_SPADES_SETTING(cg_sentryPuffRadius, "0.3"); // puff radius at birth, blocks
DEFINE_SPADES_SETTING(cg_sentryPuffGrowth, "6");   // radius gained per second, blocks/s
DEFINE_SPADES_SETTING(cg_sentryPuffLife, "0.15"); // total lifetime, s
// ========================================================================

// cg_particles is DEFINE'd in Client_LocalEnts.cpp; declared here so
// the sentry's casing water splashes respect the particle setting.
SPADES_SETTING(cg_particles);

namespace spades {
	namespace client {

		// Per-level muzzle distance from the head pivot (v27):
		// each turret tier has its own gun length, so the tracer,
		// smoke trail, flash puff and muzzle light all start at
		// the right spot. Live-tunable in the console; freeze the
		// winning values in the SENTRY CONFIG block.
		static float SentryMuzzleDistance(int level) {
			switch (level) {
			case 1:
				return (float)cg_sentry1Muzzle;
			case 2:
				return (float)cg_sentry2Muzzle;
			default:
				return (float)cg_sentry3Muzzle;
			}
		}

		// ---- Level-3 minigun bullet smoke trail ------------------------
		// Ported from the first-person M134 minigun skin: on each shot
		// a trail races from the muzzle along the firing direction
		// (frozen at the shot), dropping a line of short-lived puffs.
		// Each puff carries the distance fade of its spawn point
		// (1 at the muzzle, 0 at the draw limit), so every trail
		// dissolves progressively toward its range end. The puffs
		// themselves are ParticleSpriteEntity instances: stationary
		// growing billboards (the skin's grow-then-collapse profile
		// is approximated by radius growth + a fade-out).
		class SentrySmokeTrail : public ILocalEntity {
			Client &client;
			Vector3 origin; // muzzle at the shot (frozen)
			Vector3 dir;    // firing direction (frozen)
			Handle<IImage> image; // Textures/WaterExpl.png (base pak)
			float distance = 0.f; // flown distance, world units
			float nextPuff = 0.f;  // distance of the next puff to drop

			static constexpr float Speed = 220.f;       // trail speed, units/s
			static constexpr float MaxDistance = 2.f;    // draw limit (v32: shortened to a third, triple density)
			static constexpr float PuffSpacing = 0.05f; // one puff every N units (v32: same puff count as 6/0.15 packed in 2 units)
			static constexpr float PuffLife = 0.1f;      // s
			static constexpr float PuffRadius = 0.1f;    // at birth
			static constexpr float PuffGrowth = 0.5f;    // radius gained over life
			static constexpr float PuffSpin = 1.6f;      // billboard spin, rad/s

		public:
			SentrySmokeTrail(Client &cli, Vector3 muzzle, Vector3 fireDir)
			    : client(cli), origin(muzzle), dir(fireDir) {
				// RegisterImage returns null SILENTLY when the file
				// is missing (unlike RegisterModel, which throws):
				// a missing sprite just disables the puffs
				try {
					image = client.GetRenderer().RegisterImage(
					  "Textures/WaterExpl.png");
				} catch (...) {
				}
			}

			bool Update(float dt) override {
				distance += Speed * dt;
				while (nextPuff <= distance &&
				       nextPuff <= MaxDistance) {
					if (image) {
						float fade = 1.f - nextPuff / MaxDistance;
						auto puff =
						  stmp::make_unique<ParticleSpriteEntity>(
						    client, image,
						    MakeVector4(1.f, 1.f, 1.f, fade));
						// stationary puff: the TRAIL moves, the
						// puffs stay where they were dropped
						puff->SetTrajectory(
						  origin + dir * nextPuff,
						  MakeVector3(0.f, 0.f, 0.f));
						puff->SetRotation(0.f, PuffSpin);
						// grow over the whole life (radius + growth
						// both scaled by the spawn fade, like the
						// skin's radius * fade)
						puff->SetRadius(
						  PuffRadius * fade,
						  PuffGrowth * fade / PuffLife);
						// the skin's collapse-to-zero is replaced by
						// a fade-out on the same short lifetime
						puff->SetLifeTime(PuffLife, 0.02f, 0.03f);
						client.AddLocalEntity(std::move(puff));
					}
					nextPuff += PuffSpacing;
				}
				return distance < MaxDistance;
			}
			void Render3D() override {}
		};

		// ---- Ejected shell casing (sentry-local) ------------------------
		// The same flight, bounce and sound behavior as the vanilla
		// GunCasing local entity (the one ClientPlayer::EjectedBrass
		// spawns for third-person players), plus one extra: a render
		// size multiplier. Defined in this file so the sentry feature
		// never modifies the vanilla GunCasing files; scale 1 renders
		// exactly the vanilla casing size.
		class SentryCasing : public ILocalEntity {
			Client &client;
			IRenderer &renderer;
			IModel *model;
			Matrix4 matrix;
			Vector3 velocity;
			int rotDir;
			float time;
			float scale; // render size multiplier (1 = vanilla)

			IAudioChunk *dropSound;
			IAudioChunk *waterSound;

		public:
			SentryCasing(Client &cli, IModel *model,
			             IAudioChunk *dropSound,
			             IAudioChunk *waterSound, Vector3 pos,
			             Vector3 dir, Vector3 flyDir,
			             float scale)
			    : client(cli), renderer(cli.GetRenderer()),
			      model(model), dropSound(dropSound),
			      waterSound(waterSound), scale(scale) {
				if (dropSound)
					dropSound->AddRef();
				if (waterSound)
					waterSound->AddRef();

				Vector3 up = MakeVector3(0, 0, 1);
				Vector3 right =
				  Vector3::Cross(dir, up).Normalize();
				up = Vector3::Cross(right, dir);

				matrix = Matrix4::FromAxis(right, dir, up, pos);
				velocity = flyDir * 10.0F;
				rotDir = SampleRandom() & 3; // random initial dir
				time = 1.0F;
			}
			~SentryCasing() {
				if (dropSound)
					dropSound->Release();
				if (waterSound)
					waterSound->Release();
			}
			bool Update(float dt) override {
				time -= 1.0F / 5.0F * dt;

				const auto &viewOrigin =
				  client.GetLastSceneDef().viewOrigin;
				float distSqr =
				  (viewOrigin - matrix.GetOrigin())
				    .GetSquaredLength2D();
				if (distSqr > FOG_DISTANCE_SQ)
					return false;

				if (time <= 0.0F)
					return false;

				Matrix4 lastMat = matrix;

				matrix = matrix * Matrix4::Rotate(
				  MakeVector3(1, 0, 0),
				  ((rotDir & 1) ? 1.0F : -1.0F) * dt * 5.0F);
				matrix = matrix * Matrix4::Rotate(
				  MakeVector3(0, 1, 0),
				  ((rotDir & 2) ? 1.0F : -1.0F) * dt * 5.0F);
				matrix =
				  Matrix4::Translate(velocity * dt) * matrix;
				velocity.z += dt * 32.0F;

				const Handle<GameMap> &map =
				  client.GetWorld()->GetMap();
				SPAssert(map);

				// Collision
				IntVector3 lp = matrix.GetOrigin().Floor();

				if (lp.z >= 63) { // dropped into water
					if (waterSound) {
						if (!client.IsMuted() &&
						    distSqr <
						      CASING_BOUNCE_DIST_SQ) {
							IAudioDevice &dev =
							  client.GetAudioDevice();
							AudioParam param;
							param.referenceDistance = 0.6F;
							param.pitch = 0.9F +
							  SampleRandomFloat() *
							    0.2F;
							dev.Play(waterSound,
							         lastMat.GetOrigin(),
							         param);
						}
						waterSound = NULL;
					}

					if (cg_particles &&
					    distSqr < CASING_BOUNCE_DIST_SQ) {
						Handle<IImage> img =
						  renderer.RegisterWhiteImage();

						Vector4 col = {1, 1, 1, 0.6F};
						Vector3 pt = matrix.GetOrigin();
						pt.z = 62.99F;

						int splats = SampleRandomInt(1, 3);
						for (int i = 0; i < splats; i++) {
							auto ent = stmp::make_unique<
							  ParticleSpriteEntity>(
							  client, img, col);
							ent->SetTrajectory(
							  pt,
							  MakeVector3(
							    SampleRandomFloat() -
							      SampleRandomFloat(),
							    SampleRandomFloat() -
							      SampleRandomFloat(),
							    -SampleRandomFloat()) *
							    2.0F,
							  1.0F, 0.4F);
							ent->SetRotation(
							  SampleRandomFloat() *
							  kTwoPi);
							ent->SetRadius(
							  0.1F +
							  SampleRandomFloat() *
							    SampleRandomFloat() *
							    0.1F);
							ent->SetLifeTime(2.0F, 0.0F, 1.0F);
							client.AddLocalEntity(
							  std::move(ent));
						}
					}

					return false;
				}

				if (map->ClipWorld(lp.x, lp.y, lp.z)) { // hit a wall
					IntVector3 lp2 =
					  lastMat.GetOrigin().Floor();
					if (lp.z != lp2.z &&
					    ((lp.x == lp2.x &&
					      lp.y == lp2.y) ||
					     !map->ClipWorld(lp.x, lp.y, lp2.z))) {
						velocity.z = -velocity.z;
						if (lp2.z < lp.z) { // ground hit
							if (dropSound) {
								if (!client.IsMuted() &&
								    distSqr <
								      CASING_BOUNCE_DIST_SQ) {
									IAudioDevice &dev =
									  client
									    .GetAudioDevice();
									AudioParam param;
									param.referenceDistance = 0.6F;
									dev.Play(dropSound,
									         lastMat
									           .GetOrigin(),
									         param);
								}
								dropSound = NULL;
							}
						}
					} else if (lp.x != lp2.x &&
					           ((lp.y == lp2.y &&
					             lp.z == lp2.z) ||
					            !map->ClipWorld(lp2.x, lp.y,
					                    lp.z))) {
						velocity.x = -velocity.x;
					} else if (lp.y != lp2.y &&
					           ((lp.x == lp2.x &&
					             lp.z == lp2.z) ||
					            !map->ClipWorld(lp.x, lp2.y,
					                    lp.z))) {
						velocity.y = -velocity.y;
					} else {
						return false;
					}

					matrix = lastMat;
					// lose some velocity due to friction
					velocity *= 0.46F;
					rotDir = (rotDir + 1) % 4;
					time -= 0.36F;
				}

				return true;
			}

			void Render3D() override {
				ModelRenderParam param;
				// per-level render size multiplier (1 = the
				// vanilla casing size)
				param.matrix =
				  matrix * Matrix4::Scale(0.0125F * scale);
				renderer.RenderModel(*model, param);
			}
		};

		std::vector<SentryEntity *> SentryEntity::s_serverTurrets;

		// ---- Server block turrets -------------------------------------
		// The piqueserver script "sentry.py" builds turrets out of
		// two stacked map blocks, colored per tier (its
		// SENTRY_LEVELS). The server announces its block color with
		// a SetColor packet for the reserved player id, so every
		// block it then builds reaches OnServerBlockTurret with
		// the tier color in hand.

		// the server script's tier colors, in tier order (keep in
		// sync with SENTRY_LEVELS in sentry.py). Color families are
		// reserved per turret type: greens are the machine gun
		// family (this build); future types get their own family
		// (red = flamethrower, yellow = grenade launcher, blue =
		// electric). Encoding rules: a tier color is a pure family
		// hue whose brightness encodes the level (bright to dark:
		// 190, 140, 80), no channel ever reaches 255 (the red
		// family especially: r == 255 marks aim data, see
		// DecodeServerAim*), and no tier color ever equals a
		// heartbeat color (the tier greens sit 65+ below the
		// idle green in green)
		static const IntVector3 kServerSentryColors[3] = {
		  IntVector3(0, 190, 0), // tier 1: green, bright
		  IntVector3(0, 140, 0), // tier 2: green, mid
		  IntVector3(0, 80, 0), // tier 3: green, dark
		};

		int SentryEntity::LevelFromServerColor(IntVector3 color) {
			for (int i = 0; i < 3; i++) {
				if (color.x == kServerSentryColors[i].x &&
				    color.y == kServerSentryColors[i].y &&
				    color.z == kServerSentryColors[i].z)
					return i + 1;
			}
			return 0;
		}

		// tolerant variant of LevelFromServerColor, for map
		// READBACKS only (never for packet colors, which are
		// exact): World::ApplyBlockActions passes every created
		// block through GameMap::GetColorJit (+/-7 per channel)
		// before storing it, so a LIVE-placed pillar block holds
		// a color NEAR its tier color, never equal. Tolerance 8
		// covers the jitter with one step of margin. Downloaded
		// (baked) maps keep exact colors and match too
		static const int kAnchorColorTolerance = 8;

		static int LevelFromServerColorApprox(IntVector3 color) {
			for (int i = 0; i < 3; i++) {
				int dr = color.x - kServerSentryColors[i].x;
				int dg = color.y - kServerSentryColors[i].y;
				int db = color.z - kServerSentryColors[i].z;
				if (dr >= -kAnchorColorTolerance &&
				    dr <= kAnchorColorTolerance &&
				    dg >= -kAnchorColorTolerance &&
				    dg <= kAnchorColorTolerance &&
				    db >= -kAnchorColorTolerance &&
				    db <= kAnchorColorTolerance)
					return i + 1;
			}
			return 0;
		}

		// the heartbeat colors sent by sentry.py when it
		// re-builds the TOP pillar block: green = idle, red =
		// target locked (keep in sync with BLINK_IDLE_COLOR
		// and BLINK_ALERT_COLOR in sentry.py). The tier greens
		// never collide with this idle green: they sit 65 or
		// more below it in green (190, 140, 80)
		static const IntVector3 kServerSentryBlinkColors[2] = {
		  IntVector3(0, 255, 0), // idle heartbeat: green
		  IntVector3(255, 0, 0), // alert heartbeat: red
		};

		static bool IsServerBlinkColor(IntVector3 color) {
			for (int i = 0; i < 2; i++) {
				if (color.x == kServerSentryBlinkColors[i].x &&
				    color.y == kServerSentryBlinkColors[i].y &&
				    color.z == kServerSentryBlinkColors[i].z)
					return true;
			}
			return false;
		}

		// sentry.py packs its CURRENT aim direction into the
		// locked heartbeat color (its make_aim_color): g = yaw
		// in 128 steps over 360 degrees (0 = -180 degrees from
		// +Y), b = pitch in 128 steps over +/- 80 degrees
		// (positive = below the horizon; map z is down). Both
		// stay < 128, and no tier color ever has r == 255 (families
		// keep every channel below 255), so a data color never
		// collides with a tier color or the idle green (keep the
		// formulas in sync with make_aim_color in sentry.py)
		static float DecodeServerAimYaw(int q) {
			return (float)q * ((float)kTwoPi / 128.f) - (float)kPi;
		}

		static float DecodeServerAimPitch(int q) {
			return (((float)q * (160.f / 127.f) - 80.f) *
			        ((float)kPi / 180.f));
		}

		bool SentryEntity::SpawnServerTurret(Client &cli,
		                                     IAudioDevice *audio,
		                                     IntVector3 lowerBlock,
		                                     int tier) {
			// dedup: the script places TWO stacked blocks per
			// turret (same x/y, one apart on z); the first
			// packet spawns the entity, every later packet
			// (second pillar block, heartbeat toggles) only
			// confirms it
			for (SentryEntity *t : s_serverTurrets) {
				if (t->serverBlock.x == lowerBlock.x &&
				    t->serverBlock.y == lowerBlock.y) {
					int dz = t->serverBlock.z - lowerBlock.z;
					if (dz >= -1 && dz <= 1)
						return false;
				}
			}

			// hide both pillar blocks for the renderer (the
			// map keeps them solid: hitbox, raycasts,
			// destructible — the player can still spade the
			// turret by aiming at the model).
			// GLMapChunk consults GameMap::IsHidden when
			// building the chunk mesh; the physics path
			// (GameMap::IsSolid) never does
			// NOTE: World::GetMap returns a Handle<GameMap>, NOT
			// a raw pointer: "auto *map" does not compile here;
			// "auto map" copies the (refcounted) handle, and
			// Handle::operator-> forwards to the GameMap
			auto map = cli.GetWorld()->GetMap();
			map->SetHidden(lowerBlock.x, lowerBlock.y,
			               lowerBlock.z, true);
			map->SetHidden(lowerBlock.x, lowerBlock.y,
			               lowerBlock.z - 1, true);

			// the two pillar blocks are render-hidden (see
			// the SetHidden calls above), so the model must
			// REPLACE them on the screen: anchor its base
			// at the ground face under the pillar, i.e.
			// lowerBlock.z + 1. (Map z points down; the
			// pillar spans world z [lowerBlock.z - 1,
			// lowerBlock.z + 1]. Anchoring on the pillar
			// top instead, lowerBlock.z - 1, made the
			// model float two blocks above the ground once
			// the blocks became invisible.)
			Vector3 deployPos((float)lowerBlock.x + 0.5f,
			                  (float)lowerBlock.y + 0.5f,
			                  (float)lowerBlock.z + 1.0f);
			if ((float)cg_sentryDebug != 0.f)
				SPLog("server turret tier %d spawned @ "
				      "(%d, %d, %d) (model replaces the "
				      "hidden pillar, base at the ground "
				      "face, z %d)",
				      tier, lowerBlock.x, lowerBlock.y,
				      lowerBlock.z, lowerBlock.z + 1);
			cli.AddLocalEntity(stmp::make_unique<SentryEntity>(
			  cli, audio, deployPos, tier, lowerBlock));
			return true;
		}

		void SentryEntity::OnServerBlockTurret(Client &cli,
		                                       IAudioDevice *audio,
		                                       IntVector3 blockPos,
		                                       IntVector3 color) {
			// two kinds of colors reach this hook: the tier
			// colors (both pillar blocks carry one on
			// placement) and the heartbeat colors (sentry.py
			// periodically re-builds the TOP pillar block in
			// green = idle or red = target locked, so every
			// client — vanilla included, late joiners too —
			// sees the turret state; the fork also uses
			// these packets to discover turrets it never saw
			// being placed, e.g. baked into a downloaded
			// map)
			int tier = LevelFromServerColor(color);
			bool blink = false;
			bool aimData = false;
			if (tier == 0) {
				blink = IsServerBlinkColor(color);
				if (!blink) {
					// sentry.py's locked heartbeat REPLACED the
					// plain red beat with AIM DATA: r = 255
					// marks it, g packs the yaw, b the pitch
					// (DecodeServerAim* above; keep in sync
					// with make_aim_color in sentry.py)
					aimData = color.x == 255;
					if (!aimData) {
						// every server-built block (unknown
						// player id) lands here, so stay
						// silent unless debugging
						if ((float)cg_sentryDebug != 0.f)
							SPLog("server block @ (%d, %d, %d): "
						      "color (%d, %d, %d) is not a "
						      "sentry color; no turret "
						      "spawned",
						      blockPos.x, blockPos.y,
						      blockPos.z, color.x,
						      color.y, color.z);
						return;
					}
				}
			}
			if (!(float)cg_sentryServer) {
				if ((float)cg_sentryDebug != 0.f)
					SPLog("server block @ (%d, %d, %d): "
					      "sentry color, but "
					      "cg_sentryServer is 0; no turret "
					      "spawned",
					      blockPos.x, blockPos.y,
					      blockPos.z);
				return;
			}
			if (!cli.GetWorld()) {
				if ((float)cg_sentryDebug != 0.f)
					SPLog("server block @ (%d, %d, %d): no "
					      "world yet; no turret spawned",
					      blockPos.x, blockPos.y,
					      blockPos.z);
				return;
			}

			// locked heartbeat beats STEER the model even when
			// the anchor recovery below fails (live-placed
			// blocks hold jittered colors, so an aim beat often
			// cannot prove the pillar): the packet's column
			// x/y IS the anchor column whatever block sent the
			// beat, so hand the decoded aim to the entity
			// registered there before any spawn decision.
			// v45: this used to sit at the END of this
			// function, after the "heartbeat color with no
			// tier block below" return, and was therefore
			// unreachable for aim beats on live turrets,
			// silently disabling the whole server aim sync
			// for everyone who saw the placement
			if (aimData)
				SetServerAim(blockPos.x, blockPos.y,
				             DecodeServerAimYaw(color.y),
				             DecodeServerAimPitch(color.z));

			// anchor the turret. blockPos is either the
			// LOWER pillar block (placement: it carries the
			// tier color) or the TOP pillar block (heartbeat
			// toggles). Map z points down, so when the TOP
			// block comes in the tier block sits at
			// blockPos.z + 1: read its color back from the
			// map to recover the anchor. Downloaded (baked)
			// map colors are exact, so a late joiner can
			// recover the anchor this way; live-placed blocks
			// carry the render color jitter and simply fail
			// the test, falling back to blockPos, where the
			// dedup in SpawnServerTurret catches the
			// duplicate pillar packet. The recovered tier
			// must AGREE with the packet's own tier (or the
			// packet must be a blink color), so a
			// tier-colored GROUND block below a placement
			// cannot steal the anchor
			IntVector3 lower = blockPos;
			// Handle<GameMap>, not a raw pointer: "auto map",
			// never "auto *map" (see SpawnServerTurret)
			auto map = cli.GetWorld()->GetMap();
			if (blockPos.z + 1 < (int)map->Depth() &&
			    map->IsSolidWrapped(blockPos.x, blockPos.y,
			                        blockPos.z + 1)) {
				IntVector3 below =
				  IntVectorFromColor(map->GetColorWrapped(
				    blockPos.x, blockPos.y,
				    blockPos.z + 1));
				int belowTier = LevelFromServerColor(below);
				// v45: a tier beat whose exact readback fails
				// retries with the tolerant matcher: without
				// it, every heartbeat respawn of a live-placed
				// turret would anchor one block too high (the
				// TOP pillar block) and die on the survival
				// poll (it polls the block above the pillar,
				// which is always air). Blink and aim beats
				// keep the exact matcher: they only need the
				// anchor for baked (late joiner) pillars,
				// whose downloaded colors are exact
				if (belowTier == 0 && tier != 0)
					belowTier =
					  LevelFromServerColorApprox(below);
				if (belowTier != 0 &&
				    (blink || aimData || belowTier == tier)) {
					lower.z = blockPos.z + 1;
					tier = belowTier;
				}
			}
			if ((blink || aimData) && tier == 0) {
				// a heartbeat color with no tier block below
				// is not one of ours (other scripts can also
				// build with the reserved id); ignore it
				if ((float)cg_sentryDebug != 0.f)
					SPLog("server block @ (%d, %d, %d): "
					      "heartbeat color, but no tier "
					      "block below; ignored",
					      blockPos.x, blockPos.y,
					      blockPos.z);
				return;
			}
			if (!SpawnServerTurret(cli, audio, lower, tier)) {
				if ((float)cg_sentryDebug != 0.f)
					SPLog("server block @ (%d, %d, %d): "
					      "pillar block of an already "
					      "spawned turret; skipped",
					      blockPos.x, blockPos.y,
					      blockPos.z);
			}
		}

		// hand the server's CURRENT aim (decoded from a locked
		// heartbeat color by OnServerBlockTurret) to the entity
		// anchored at the given column; Update lerps the model
		// toward it
		void SentryEntity::SetServerAim(int x, int y, float yaw,
		                               float pitch) {
			for (SentryEntity *t : s_serverTurrets) {
				if (t->serverBlockMode &&
				    t->serverBlock.x == x &&
				    t->serverBlock.y == y) {
					t->serverAimYaw = yaw;
					t->serverAimPitch = pitch;
					t->serverAimPending = true;
				}
			}
		}

		// sentry.py announces every REAL server-side shot with a
		// tiny covert SetColor packet on a reserved id (see the
		// NetClient.cpp SetColor hook): the 24-bit value packs
		// the turret's (x, y) = the anchor block column. Play
		// the visual shot at the exact moment the damage lands
		void SentryEntity::OnServerSentryFire(int packedPos) {
			int x = packedPos & 511;
			int y = (packedPos >> 9) & 511;
			for (SentryEntity *t : s_serverTurrets) {
				if (t->serverBlockMode &&
				    t->serverBlock.x == x &&
				    t->serverBlock.y == y) {
					// stacked turrets share a column and the
					// packet carries no z: flash them all
					t->ServerFire();
				}
			}
		}

		// lift the client-side render hiding of a server
		// turret's two pillar blocks; called when the turret
		// dies, so a block later rebuilt at the same coords
		// renders normally. z points down: the upper pillar
		// block sits at z - 1 (SetHidden guards out-of-range
		// z). Note: NOT called from the destructor on
		// purpose — during world teardown the map renderer
		// may already be gone, and the flags die with the map
		static void UnhidePillarBlocksFor(Client &cli,
		                                  IntVector3 serverBlock) {
			auto *world = cli.GetWorld();
			if (!world)
				return;
			// Handle<GameMap>, not a raw pointer: "auto map",
			// never "auto *map" (see SpawnServerTurret)
			auto map = world->GetMap();
			map->SetHidden(serverBlock.x, serverBlock.y,
			               serverBlock.z, false);
			map->SetHidden(serverBlock.x, serverBlock.y,
			               serverBlock.z - 1, false);
		}

		SentryEntity::SentryEntity(Client &cli, IAudioDevice *audio,
		                           Vector3 deployPos, int deployLevel,
		                           IntVector3 serverBlockPos)
		    : client(cli), audioDevice(audio),
		      level(Clamp(deployLevel, 1, 3)), position(deployPos),
		      serverBlock(serverBlockPos) {
			SPADES_MARK_FUNCTION();

			// initial team filter (the sentry targets the enemies of
			// the local player's team; refreshed on every scan so
			// modes reshuffling teams between phases cannot blind it)
			myTeamId = 0;
			if (client.GetWorld()) {
				auto lp = client.GetWorld()->GetLocalPlayer();
				if (lp)
					myTeamId = lp->GetTeamId();
			}

			// shot samples: four random variations (like the SMG),
			// custom assets in Sounds/Turrets/MG<level>/. RegisterSound
			// throws when the file is missing, so each registration
			// is guarded: a missing sample stays silent instead of
			// crashing the game. NO fallback to another tier's
			// assets: a not-yet-built MG2/MG3 turret is silent but
			// otherwise fully functional.
			if (audioDevice && !client.IsMuted()) {
				for (int i = 0; i < FireSampleCount; i++) {
					char path[96];
					snprintf(path, sizeof(path),
					         "Sounds/Turrets/MG%d/FireTurretMG%d.opus",
					         level, i + 1);
					try {
						fireChunks[i] =
						  audioDevice->RegisterSound(path);
					} catch (...) {
					}
				}

				// v28: level-3 barrel spin tick, the same
				// 2-per-revolution tick as the first-person
				// minigun skin; custom asset in the tier
				// folder, guarded like the fire samples
				// (a missing file = silent spin)
				if (level == 3) {
					try {
						spinTickSound =
						  audioDevice->RegisterSound(
						    "Sounds/Turrets/MG3/"
						    "SpinTick.opus");
					} catch (...) {
					}
				}
			}

			// models: RegisterModel throws when the file is missing
			// (unlike RegisterImage, which returns null silently), so
			// each registration is guarded: a missing model leaves the
			// sentry alive but invisible instead of crashing the game.
			// The per-tier folders reuse the same file names.
			auto *r = &client.GetRenderer();
			char path[96];
			snprintf(path, sizeof(path),
			         "Models/Turrets/MG%d/TurretMGBase.kv6", level);
			try {
				baseModel = r->RegisterModel(path);
			} catch (...) {
			}
			snprintf(path, sizeof(path),
			         "Models/Turrets/MG%d/TurretMG.kv6", level);
			try {
				headModel = r->RegisterModel(path);
			} catch (...) {
			}

			// level 3 only: the minigun's spinning barrels are a
			// SEPARATE model; the level-3 head is the body WITHOUT
			// the barrels. The kv6's own pivot does NOT need to
			// sit on the barrel axis: Render3D spins the model
			// around the TRUE barrel axis via the console-tunable
			// cg_sentryBarrelPivot{X,Z} offset (T(offset)*R*
			// T(-offset), same as the first-person minigun skin).
			if (level == 3) {
				try {
					barrelsModel = r->RegisterModel(
					  "Models/Turrets/MG3/TurretMGBarrels.kv6");
				} catch (...) {
				}
			}

			// ejected shell casing model (custom asset, like the
			// third-person players' Casing.kv6); the drop/water
			// sounds are vanilla SMG casing sounds (base assets)
			snprintf(path, sizeof(path),
			         "Models/Turrets/MG%d/Casing.kv6", level);
			try {
				casingModel = r->RegisterModel(path);
			} catch (...) {
			}
			if (audioDevice && !client.IsMuted()) {
				try {
					casingDropSound1 = audioDevice->RegisterSound(
					  "Sounds/Weapons/SMG/ShellDrop1.opus");
					casingDropSound2 = audioDevice->RegisterSound(
					  "Sounds/Weapons/SMG/ShellDrop2.opus");
					casingWaterSound = audioDevice->RegisterSound(
					  "Sounds/Weapons/SMG/ShellWater.opus");
				} catch (...) {
				}
			}

			// initial head orientation: face the deploying player
			if (client.GetWorld()) {
				auto lp = client.GetWorld()->GetLocalPlayer();
				if (lp) {
					Vector3 d = lp->GetEye() - HeadOrigin();
					d.z = 0.f;
					if (d.GetSquaredLength() > 0.001f)
						currentYaw = atan2f(d.x, d.y);
				}
			}

			// server block turret bookkeeping (see
			// OnServerBlockTurret); plain local deploys pass no
			// block (the default sentinel, negative x)
			serverBlockMode = serverBlockPos.x >= 0;
			if (serverBlockMode)
				s_serverTurrets.push_back(this);
		}

		SentryEntity::~SentryEntity() {
			SPADES_MARK_FUNCTION();
			if (serverBlockMode) {
				auto it = std::find(s_serverTurrets.begin(),
				                    s_serverTurrets.end(), this);
				if (it != s_serverTurrets.end())
					s_serverTurrets.erase(it);
			}
		}

		float SentryEntity::FireIntervalSeconds() const {
			// Per-tier rate of fire, all derived from the ONE live-
			// tunable base so console calibration shifts the whole
			// ladder proportionally. Defaults: 1 = 0.50 s,
			// 2 = ~0.33 s, 3 = 0.25 s.
			float base = (float)cg_sentryFireInterval;
			switch (level) {
				case 2:
					return base * (2.f / 3.f);
				case 3:
					return base * 0.5f;
				default:
					return base;
			}
		}

		Vector3 SentryEntity::HeadOrigin() const {
			// The head rests on TOP of the base: the auto height is
			// the base model's bounding box height, so it follows
			// the render scale and whatever base model is
			// registered (future upgraded turrets).
			//
			// CAVEAT (source-verified, GLOptimizedVoxelModel.cpp:
			// the bounding box is origin..origin + dimensions):
			// IModel::GetBoundingBox() is NOT voxel-exact. It is
			// the model's FULL kv6 canvas box, including any empty
			// space around the voxels drawn in Slab6: the base
			// canvas can be taller than the tripod, and the head
			// canvas says nothing about where the gun's lowest
			// voxel is. The auto height therefore lands the head
			// at the top of the base's CANVAS, and cg_sentryHeadZ
			// seats it on the actual mount: a one-time manual
			// offset in world blocks (negative = lower), calibrated
			// live in the console then frozen here. (Building the
			// kv6 models tight in their canvases makes the auto
			// part exact.)
			//
			// Z in map/model space points DOWN (the kv6 loader
			// keeps the file's z-down axis, see VoxelModel.cpp:
			// SetOrigin(-pivot); in the AABB, min.z is the TOP of
			// the canvas and max.z its BOTTOM edge), so the
			// height is SUBTRACTED to move UP.
			//
			// The fallback (base model missing) keeps the head
			// near the ground.
			float headZ = 0.5f;
			if (baseModel) {
				AABB3 bb = baseModel->GetBoundingBox();
				headZ = (bb.max.z - bb.min.z) * (float)cg_sentryScale;
			}
			headZ += (float)cg_sentryHeadZ;
			return position - MakeVector3(0.f, 0.f, headZ);
		}

		Vector3 SentryEntity::HeadForward() const {
			// yaw 0 = +Y (the model's forward axis)
			return MakeVector3(sinf(currentYaw), cosf(currentYaw), 0.f);
		}

		Vector3 SentryEntity::AimForward() const {
			// aim direction = horizontal forward tilted by the
			// gun pitch. Map z is down, so a POSITIVE pitch adds a
			// +z (downward) component: the gun points below the
			// horizon at lower targets.
			return HeadForward() * cosf(currentPitch) +
			       MakeVector3(0.f, 0.f, 1.f) * sinf(currentPitch);
		}

		Player *SentryEntity::FindTarget() {
			World *world = client.GetWorld();
			if (!world)
				return nullptr;

			// keep the team filter in sync with the local player's
			// CURRENT team: game modes may reshuffle teams between
			// phases (e.g. bots deactivating and reactivating), and
			// a filter frozen at deploy time would blind the turret
			// to former enemies forever. The last known value stays
			// as the fallback while the local player is gone.
			auto lp = world->GetLocalPlayer();
			if (lp)
				myTeamId = lp->GetTeamId();

			float range = (float)cg_sentryRange;
			float best = range * range;
			Player *bestPlayer = nullptr;
			Vector3 origin = HeadOrigin();

			// live diagnostic: with cg_sentryDebug 1, once per
			// second the console log (SystemMessages.log)
			// explains why the scan keeps or rejects every
			// connected player — team, alive, distance, line
			// of sight — so a turret that seems blind can be
			// decoded without guessing (e.g. modes with team
			// ids >= 2, which the engine flags as spectators).
			// The throttle is a MEMBER: several sentries can
			// coexist (the "/sentry" command spawns one per
			// call), and each logs its OWN verdicts — the log
			// line carries the sentry's position to tell
			// coexisting turrets apart when diagnosing why one
			// fires and another stays silent.
			bool debugLog = false;
			if ((float)cg_sentryDebug != 0.f) {
				float now = world->GetTime();
				if (now >= nextDebugLog) {
					nextDebugLog = now + 1.f;
					debugLog = true;
				}

				// sensor probe, once per debug cycle: CastRay2
				// tests the START voxel first (source-verified,
				// GameMap.cpp: IsSolidWrapped on v0.Floor() ->
				// hit = startSolid = true immediately), so a
				// one-step ray flags whether the head origin
				// itself sits inside a solid block. Such a sentry
				// is blind in ALL directions (360°) — the classic
				// deployed-against-a-wall bug — and the per-
				// player lines below would all read los=0.
				auto map = world->GetMap();
				if (map) {
					GameMap::RayCastResult probe =
					  map->CastRay2(origin,
					                MakeVector3(0.f, 0.f, 1.f), 1);
					if (probe.startSolid)
						SPLog("sentry scan @ (%.1f, %.1f, %.1f): "
						      "SENSOR INSIDE A BLOCK (startSolid): "
						      "this sentry is blind in ALL directions",
						      origin.x, origin.y, origin.z);
				}
			}

			// v25: "any bit of head visible = targetable".
			// The old single eye-ray dropped targets as soon
			// as their EYE alone went behind a ground edge.
			// Now the eye is tried first (the cheap common
			// case), then four more points around the head:
			// top, chin and both cheeks. All rays still stop
			// at solid blocks, so a fully hidden head remains
			// a fully ignored target; the extra rays are paid
			// only when the eye ray was blocked.
			auto headVisible = [&](Player &p) -> bool {
				Vector3 eye = p.GetEye();
				if (HasLineOfSight(origin, eye))
					return true;
				Vector3 toEye = eye - origin;
				if (toEye.GetSquaredLength() < 0.0001f)
					return true;
				Vector3 up = MakeVector3(0.f, 0.f, -1.f);
				Vector3 lateral = Vector3::Cross(
				  up, toEye.Normalize()).Normalize();
				return
				  HasLineOfSight(origin, eye + up * 0.35f) ||
				  HasLineOfSight(origin, eye - up * 0.35f) ||
				  HasLineOfSight(
				    origin, eye + lateral * 0.25f) ||
				  HasLineOfSight(
				    origin, eye - lateral * 0.25f);
			};

			size_t n = world->GetNumPlayerSlots();
			for (size_t i = 0; i < n; i++) {
				stmp::optional<Player&> maybe =
				  world->GetPlayer((unsigned int)i);
				if (!maybe)
					continue;
				Player &p = *maybe;

				if (debugLog) {
					bool alive = p.IsAlive() && !p.IsSpectator();
					bool ally = p.GetTeamId() == myTeamId;
					float dist =
					  sqrtf((p.GetEye() - origin).GetSquaredLength());
					bool los = alive && !ally && dist <= range &&
					           headVisible(p);
					SPLog("sentry scan @ (%.1f, %.1f, %.1f): "
					      "'%s' team=%d alive=%d "
					      "ally=%d d=%.1f inRange=%d los=%d%s",
					      origin.x, origin.y, origin.z,
					      p.GetName().c_str(), (int)p.GetTeamId(),
					      (int)alive, (int)ally, dist,
					      (int)(dist <= range), (int)los,
					      los ? " <- candidate" : "");
				}

				if (!p.IsAlive() || p.IsSpectator())
					continue;
				if (p.GetTeamId() == myTeamId)
					continue;

				float d = (p.GetEye() - origin).GetSquaredLength();
				if (d < best) {
					// head-visibility: a target hidden
					// behind solid blocks is ignored (the
					// sentry never tracks or shoots through
					// walls; one visible bit of head is
					// enough). The raycasts are paid only
					// for candidates that would beat the
					// current best, so the per-frame cost
					// stays at a handful of map raycasts
					// even on a busy server; a blocked
					// nearer enemy does NOT consume
					// "best", a visible enemy farther away
					// can still be picked.
					if (!headVisible(p))
						continue;
					best = d;
					bestPlayer = &p;
				}
			}
			return bestPlayer;
		}

		bool SentryEntity::HasLineOfSight(Vector3 from, Vector3 to) const {
			// map-only raycast (the engine's own CastRay2, the very
			// one World::WeaponRayCast uses): solid BLOCKS block the
			// sentry's sight; players never block target selection
			// here (Fire's own raycast still stops the visual tracer
			// at a player standing on the sight line).
			//
			// The ray starts at the head origin: the "sensor" point of
			// the turret, stable whatever the gun pitch currently is.
			// CastRay2 steps voxel by voxel (maxSteps crossings), so
			// LOSRaySteps must cover the target range even on a fully
			// diagonal path: 256, same as the engine's weapon raycast.
			Vector3 dir = to - from;
			float len = dir.GetLength();
			if (len < 0.001f)
				return true; // same point: nothing can occlude

			dir = dir * (1.f / len);

			if (!client.GetWorld())
				return true; // no map: do not blind the sentry
			auto map = client.GetWorld()->GetMap();
			if (!map)
				return true;

			// hit (including startSolid, the sensor itself inside a
			// block) = something solid on the way: blocked sight
			GameMap::RayCastResult res =
			  map->CastRay2(from, dir, LOSRaySteps);
			return !res.hit;
		}

		void SentryEntity::Fire(Player &target) {
			SPADES_MARK_FUNCTION();

			// v27: the muzzle distance is per level (each
			// turret tier has its own gun length). v31: level 3
			// also gets a vertical trim (cg_sentry3MuzzleZ,
			// positive = up, same sign convention as
			// cg_sentryHeadZ): map z is DOWN, so subtracting
			// the value raises the muzzle
			Vector3 muzzle = HeadOrigin() +
			  AimForward() * SentryMuzzleDistance(level);
			if (level == 3)
				muzzle.z -= (float)cg_sentry3MuzzleZ;
			// v34: the aim point is the target's BODY (below the
			// head), not the eye: the tracer and the weapon
			// raycast point at the torso, where shots would
			// actually land once the turret deals damage.
			// Map z is DOWN, so +z = below the eye.
			// cg_sentryAimBody = 0 restores the old eye-level
			// aim; the head-visibility check above still uses
			// the eye (that is the VISION test, not the aim).
			Vector3 aimPoint = target.GetEye() +
			  MakeVector3(0.f, 0.f, (float)cg_sentryAimBody);
			Vector3 aim = aimPoint - muzzle;
			// degenerate aim (target's eye on the muzzle, e.g. an
			// enemy standing inside the sentry): the tracer would
			// be zero-length; skip the shot entirely
			if (aim.GetSquaredLength() < 0.0001f)
				return;
			FireVisual(aim.Normalize());
		}

		// shared visual shot (tracer, muzzle flash, sound, shell
		// casing, level-3 barrel kick) along the given direction;
		// used by the local cosmetic Fire above and by the
		// server-synced ServerFire below, so both modes show the
		// exact same shot
		void SentryEntity::FireVisual(Vector3 aimDir) {

			// the muzzle point is recomputed here (Fire keeps
			// its own copy for the aim vector; within one frame
			// both are identical): same per-level gun length
			// (v27) and level-3 vertical trim (v31, map z is
			// DOWN, so subtracting raises the muzzle)
			Vector3 muzzle = HeadOrigin() +
			  AimForward() * SentryMuzzleDistance(level);
			if (level == 3)
				muzzle.z -= (float)cg_sentry3MuzzleZ;

			// endpoint: first block on the sight line (the local
			// player cannot block their own sentry); no hit = the
			// tracer flies out to the range limit
			Vector3 hitPos = muzzle + aimDir * (float)cg_sentryRange;
			if (client.GetWorld()) {
				stmp::optional<int> excludePlayer{};
				auto lp = client.GetWorld()->GetLocalPlayer();
				if (lp)
					excludePlayer = lp->GetId();

				World::WeaponRayCastResult res =
				  client.GetWorld()->WeaponRayCast(muzzle, aimDir,
				                                   excludePlayer);
				if (res.hit)
					hitPos = res.hitPos;
			}

			// muzzle inside a solid block (startSolid makes the
			// raycast return the muzzle itself) or a zero-length
			// hit: keep a minimal tracer length so the shot is
			// always visible and never degenerate
			if ((hitPos - muzzle).GetSquaredLength() < 0.0001f)
				hitPos = muzzle + aimDir * 0.5f;

			// engine tracer: the very same entity the rifles use
			client.AddLocalEntity(stmp::make_unique<Tracer>(
			  client, muzzle, hitPos, BulletVel, false));

			// v32: vanilla 3D sound handling restored (at the
			// maintainer's request): the engine's own
			// spatialized play at the muzzle position, with its
			// distance rolloff (referenceDistance 5, the value
			// the vanilla death/grenade sounds use). The former
			// 2D PlayLocal workaround (manual distance curve
			// kept the turret audible at any range but without
			// spatialization) is gone; cg_sentryVolume still
			// scales the loudness.
			if (audioDevice) {
				AudioParam param;
				param.referenceDistance = 5.f;
				param.volume = (float)cg_sentryVolume;
				// random pick among the four samples, like the SMG.
				// CAVEAT (source-verified, ALDevice.cpp: the 3D
				// Play dereferences the chunk with NO null
				// check) : a missing sample file leaves the
				// chunk null, and playing a null chunk CRASHES
				// the game, so the shot goes silent instead
				// via this guard
				Handle<IAudioChunk> &chunk =
				  fireChunks[SampleRandomInt(0, FireSampleCount - 1)];
				if (chunk) {
					audioDevice->Play(chunk.GetPointerOrNull(),
					                 muzzle, param);
				}
			}

			// ejected shell casing: the sentry's own casing
			// entity (SentryCasing, defined at the top of this
			// file): the same flight, bounce and drop-sound
			// behavior as the vanilla GunCasing the
			// third-person players use (ClientPlayer::
			// EjectedBrass), thrown from the head pivot
			// (breech area) rather than the muzzle. v25
			// directions: levels 1-2 throw to the gun's LEFT
			// and BEHIND it (aim component flipped,
			// live-tunable via cg_sentryCasingFwd); level 3
			// (minigun) keeps the ORIGINAL vanilla ejection
			// side: RIGHT, forward throw. Casings also get a
			// per-level size: level 1 = the vanilla size,
			// level 2 = +25%, level 3 = +50%. v29: the
			// ejection SEAT stays the head pivot for levels
			// 1-2 (validated in game); level 3 gets a
			// live-tunable forward offset to sit the
			// ejection right on the minigun's body
			// (cg_sentry3CasingFwd, 0 = the pivot).
			if (casingModel) {
				Vector3 up = MakeVector3(0.f, 0.f, -1.f);
				Vector3 left =
				  Vector3::Cross(up, aimDir).Normalize();
				Vector3 flyDir;
				float casingScale = 1.f;
				Vector3 casingOrigin = HeadOrigin();
				if (level == 3) {
					// original vanilla-side ejection:
					// right = -left, forward throw
					flyDir =
					  aimDir * 0.5f - left + up * 0.2f;
					casingScale =
					  (float)cg_sentry3CasingScale;
					// v29: seat the ejection forward
					// along the gun (0 = the pivot)
					casingOrigin +=
					  AimForward() *
					  (float)cg_sentry3CasingFwd;
				} else {
					flyDir =
					  aimDir * (float)cg_sentryCasingFwd +
					  left + up * 0.2f;
					if (level == 2)
						casingScale =
						  (float)cg_sentry2CasingScale;
				}
				client.AddLocalEntity(stmp::make_unique<SentryCasing>(
				  client, casingModel.GetPointerOrNull(),
				  SampleRandomBool()
				    ? casingDropSound1.GetPointerOrNull()
				    : casingDropSound2.GetPointerOrNull(),
				  casingWaterSound.GetPointerOrNull(),
				  casingOrigin, aimDir, flyDir,
				  casingScale));
			}

			flashTimer = FlashTime;

			// level-3 minigun: each shot kicks the barrel spin up
			// (clamped at the top speed, wind-down happens in
			// Update), and launches a bullet smoke trail like the
			// first-person minigun's
			if (level == 3) {
				barrelsSpinVel = Clamp(barrelsSpinVel + BarrelSpinAccel,
				                       0.f, BarrelSpinMax);
				client.AddLocalEntity(stmp::make_unique<SentrySmokeTrail>(
				  client, muzzle, aimDir));
			} else {
				// levels 1-2: a single flash puff at the
				// muzzle to figure the shot's flash (the
				// same sprite the minigun trail uses);
				// RegisterImage is null-silent on a
				// missing file, so a missing sprite just
				// skips the puff. v29: much bigger and
				// longer-lived than the trail puffs (the
				// v25 values were near-invisible in game),
				// live-tunable via cg_sentryPuffRadius/
				// Growth/Life
				try {
					Handle<IImage> img =
					  client.GetRenderer().RegisterImage(
					    "Textures/WaterExpl.png");
					if (img) {
						auto puff =
						  stmp::make_unique<
						    ParticleSpriteEntity>(
						    client, img,
						    MakeVector4(1.f, 1.f,
						                1.f, 1.f));
						puff->SetTrajectory(
						  muzzle,
						  MakeVector3(0.f, 0.f,
						              0.f));
						// v33: random initial angle so
						// successive shots never show
						// the same side of the sprite
						puff->SetRotation(
						  SampleRandomFloat() * (float)kTwoPi,
						  1.6f);
						// pop up noticeably, then
						// fade over half the life
						puff->SetRadius(
						  (float)cg_sentryPuffRadius,
						  (float)cg_sentryPuffGrowth);
						float puffLife =
						  (float)cg_sentryPuffLife;
						puff->SetLifeTime(puffLife,
						                  0.02f,
						                  puffLife * 0.5f);
						client.AddLocalEntity(
						  std::move(puff));
					}
				} catch (...) {
				}
			}
		}

		// one REAL server-side shot (announced by sentry.py, see
		// OnServerSentryFire): fire along the LAST server aim
		// rather than the lerped head pose, which may still be
		// catching up, so the tracer matches where the damage
		// actually went
		void SentryEntity::ServerFire() {
			SPADES_MARK_FUNCTION();
			Vector3 fwd =
			  MakeVector3(sinf(serverAimYaw) *
			                cosf(serverAimPitch),
			              cosf(serverAimYaw) *
			                cosf(serverAimPitch),
			              sinf(serverAimPitch));
			FireVisual(fwd);
		}

		// v45: survival-poll grace for server block turrets.
		// The pillar blocks are created through
		// World::CreateBlock, which is DEFERRED: the map only
		// receives them in World::Advance, which the sub-frame
		// accumulator runs at most every 1/60 s. At frame
		// rates above 60 the entity's first Update usually runs
		// BEFORE its blocks exist in the map, and the poll in
		// Update would read "pillar gone" and kill the freshly
		// spawned turret (then un-hide its blocks: no model,
		// visible pillar). Skip the poll for the first moments
		// of the entity's life; after 0.5 s of game time at
		// least one Advance is guaranteed to have applied the
		// blocks
		static constexpr float ServerSpawnGrace = 0.5f;

		bool SentryEntity::Update(float dt) {
			SPADES_MARK_FUNCTION();

			age += dt;
			// a server block turret has no local age limit:
			// the server script owns its lifetime; the client
			// entity lives exactly as long as its blocks
			if (!serverBlockMode && age >= MaxAge)
				return false;

			// server block turret: the server script removes the
			// whole turret when ANY of its two blocks is
			// destroyed, so a vanished block = turret destroyed
			// = die with it. Lift the render hiding of both
			// pillar blocks so a block rebuilt there later
			// renders normally. Grace: see ServerSpawnGrace
			// above; blocks not yet applied by Advance must not
			// read as "destroyed"
			if (serverBlockMode && age >= ServerSpawnGrace) {
				auto *world = client.GetWorld();
				if (!world ||
				    !world->GetMap()->IsSolidWrapped(
				      serverBlock.x, serverBlock.y,
				      serverBlock.z) ||
				    !world->GetMap()->IsSolidWrapped(
				      serverBlock.x, serverBlock.y,
				      serverBlock.z - 1)) {
					UnhidePillarBlocksFor(client, serverBlock);
					if ((float)cg_sentryDebug != 0.f)
						SPLog("server turret @ (%d, %d, %d): "
						      "pillar blocks gone; "
						      "entity removed",
						      serverBlock.x, serverBlock.y,
						      serverBlock.z);
					return false;
				}
			}

			fireTimer -= dt;
			flashTimer -= dt;

			// level-3 minigun barrels: continuous wind-down once the
			// shots stop kicking the spin (in Fire), then integrate
			// the roll angle; wrapped so hours of spinning never
			// lose float precision
			if (level == 3) {
				if (barrelsSpinVel > 0.f) {
					barrelsSpinVel -= BarrelSpinDown * dt;
					if (barrelsSpinVel < 0.f)
						barrelsSpinVel = 0.f;
				}
				barrelsRoll += barrelsSpinVel * dt;
				float wrap = (float)kTwoPi * BarrelRollWrapTurns;
				if (barrelsRoll > wrap)
					barrelsRoll -= wrap;

				// v28: barrel spin tick, 2 per revolution like
				// the first-person minigun skin. Ticks only
				// while the barrels actually spin. v32: vanilla
				// 3D play at the turret head (referenceDistance
				// 1: a close mechanical click), the tick stays
				// quiet: SpinTickVolume of the shot level.
				// The 3D Play crashes on a null chunk
				// (source-verified), hence the guards.
				if (barrelsSpinVel > SpinTickMinSpeed) {
					spinTickTimer -= dt;
					if (spinTickTimer <= 0.f) {
						if (audioDevice && spinTickSound &&
						    !client.IsMuted()) {
							AudioParam param;
							param.referenceDistance = 1.f;
							param.volume =
							  (float)cg_sentryVolume *
							  SpinTickVolume;
							audioDevice->Play(
							  spinTickSound
							    .GetPointerOrNull(),
							  HeadOrigin(), param);
						}
						spinTickTimer += (float)kTwoPi /
						  (barrelsSpinVel *
						   SpinTicksPerRevolution);
					}
				} else {
					// spin too slow: re-arm the next tick
					// so the first tick of a new burst is
					// instant, like the skin
					spinTickTimer = 0.f;
				}
			}

			// v44: a server block turret NEVER picks targets
			// locally: the server owns the targeting (the old
			// local scan even aimed at the wrong side for
			// enemy turrets, since the client cannot know the
			// owner's team). The locked heartbeat colors carry
			// the server's actual aim (SetServerAim, from
			// OnServerBlockTurret); lerp the head toward it
			// while the data keeps arriving, and stand down
			// (gun back to level) once it stops coming. Real
			// shots are announced per hit by OnServerSentryFire
			// (NetClient's SetColor hook), so this branch
			// never fires on its own
			if (serverBlockMode) {
				if (serverAimPending) {
					serverAimPending = false;
					serverAimTime = age;
				}
				bool aimFresh =
				  age - serverAimTime < ServerAimTimeout;
				hasTarget = aimFresh;

				float pitchLimit =
				  MaxAimPitchDeg * (float)kPi / 180.f;
				float desiredPitch = 0.f;
				float desiredYaw = currentYaw;
				if (aimFresh) {
					desiredPitch =
					  Clamp(serverAimPitch, -pitchLimit,
					        pitchLimit);
					desiredYaw = serverAimYaw;
				}
				currentPitch +=
				  Clamp(desiredPitch - currentPitch,
				        -TurnSpeed * dt, TurnSpeed * dt);
				float remain = desiredYaw - currentYaw;
				while (remain > (float)kPi)
					remain -= (float)kTwoPi;
				while (remain < -(float)kPi)
					remain += (float)kTwoPi;
				currentYaw +=
				  Clamp(remain, -TurnSpeed * dt,
				        TurnSpeed * dt);
				return true;
			}

			Player *target = FindTarget();
			hasTarget = target != nullptr;

			// vertical aim: pitch the gun at the target, relaxing
			// back to level when idle. Map z is down, so a target
			// LOWER than the sentry gives a positive dz = a
			// positive (below-horizon) pitch: the turret can aim
			// at players below or above itself, clamped to
			// MaxAimPitchDeg. cg_sentryAimPitch 0 keeps the gun
			// horizontal (previous behavior).
			float desiredPitch = 0.f;
			if (target && (float)cg_sentryAimPitch != 0.f) {
				Vector3 dv = target->GetEye() - HeadOrigin();
				float hDist = sqrtf(dv.x * dv.x + dv.y * dv.y);
				if (hDist > 0.001f)
					desiredPitch = atan2f(dv.z, hDist);
			}
			float pitchLimit = MaxAimPitchDeg * (float)kPi / 180.f;
			desiredPitch =
			  Clamp(desiredPitch, -pitchLimit, pitchLimit);
			currentPitch +=
			  Clamp(desiredPitch - currentPitch, -TurnSpeed * dt,
			        TurnSpeed * dt);

			if (target) {
				// horizontal aim toward the target
				Vector3 d = target->GetEye() - HeadOrigin();
				d.z = 0.f;
				if (d.GetSquaredLength() > 0.001f) {
					float desiredYaw = atan2f(d.x, d.y);

					// shortest signed angle to the target
					float remain = desiredYaw - currentYaw;
					while (remain > (float)kPi)
						remain -= (float)kTwoPi;
					while (remain < -(float)kPi)
						remain += (float)kTwoPi;

					currentYaw +=
					  Clamp(remain, -TurnSpeed * dt, TurnSpeed * dt);

					// inside the aim cone (yaw AND pitch): shoot
					float aimCone = AimConeDeg * (float)kPi / 180.f;
					if (fabsf(remain) < aimCone &&
					    fabsf(desiredPitch - currentPitch) <
					      aimCone &&
					    fireTimer <= 0.f) {
						fireTimer = FireIntervalSeconds();
						Fire(*target);
					}
				}
			}

			return true;
		}

		void SentryEntity::Render3D() {
			SPADES_MARK_FUNCTION();
			auto *r = &client.GetRenderer();

			float d2r = (float)kPi / 180.f;
			float scale = (float)cg_sentryScale;

			// NOTE: map space and kv6 model space share the same
			// z-down axis convention (particles rise with a
			// negative vel.z; the kv6 loader keeps the file's z
			// axis, see VoxelModel.cpp), so mapping the model z
			// onto +Z renders upright models. The rocket entity's
			// old "world up (0,0,1)" comment was wrong.
			Vector3 up = MakeVector3(0.f, 0.f, 1.f);

			// constant orientation corrections, applied to BOTH
			// models (calibrate live, then freeze the values)
			Matrix4 fix =
			  Matrix4::Rotate(MakeVector3(0.f, 0.f, 1.f),
			                  (float)cg_sentryYawFix * d2r) *
			  Matrix4::Rotate(MakeVector3(1.f, 0.f, 0.f),
			                  (float)cg_sentryPitchFix * d2r) *
			  Matrix4::Rotate(MakeVector3(0.f, 1.f, 0.f),
			                  (float)cg_sentryRollFix * d2r);

			// --- base: fixed at the deployment point, on the
			//     ground. bb.max.z is the BOTTOM edge of the
			//     base's canvas box (z-down; the bbox already
			//     includes the -pivot - 0.5 origin shift), so
			//     shifting the origin by -max.z * scale drops
			//     that edge exactly onto the deployment point.
			//     With a base model built tight to the bottom of
			//     its Slab6 canvas, this is the feet ---
			if (baseModel) {
				AABB3 bb = baseModel->GetBoundingBox();
				Vector3 basePos =
				  position - MakeVector3(0.f, 0.f, bb.max.z) * scale;
				Matrix4 mat =
				  Matrix4::FromAxis(MakeVector3(1.f, 0.f, 0.f),
				                   MakeVector3(0.f, 1.f, 0.f), up,
				                   basePos) *
				  fix * Matrix4::Scale(scale);
				ModelRenderParam p;
				p.matrix = mat;
				r->RenderModel(*baseModel, p);
			}

			// --- head: yaw- and pitch-driven, resting on top of
			//     the base. The forward axis follows AimForward()
			//     (tilted by the gun pitch); the right axis stays
			//     horizontal so the head banks on a clean lateral
			//     hinge whatever the pitch. The level-3 minigun's
			//     body and barrels ride the SAME frame, so it is
			//     computed whenever either model is present ---
			if (headModel || barrelsModel) {
				Vector3 fwd = AimForward();
				Vector3 right =
				  Vector3::Cross(HeadForward(), up).Normalize();
				Vector3 upv = Vector3::Cross(right, fwd).Normalize();

				// level-3 fine-tuning: the minigun's body and
				// barrels get their OWN scale multipliers and seat
				// offsets (MODEL units, applied BEFORE the part's
				// scale; X = lateral with LEFT = negative,
				// Y = forward, Z = DOWN, so up = negative
				// Z), calibrated live in the console then frozen
				// in the settings above. Every other level renders
				// exactly like before: shared scale, no offset.
				float headScale = scale;
				float barrelScale = scale;
				Vector3 bodyOff = MakeVector3(0.f, 0.f, 0.f);
				Vector3 barrelOff = MakeVector3(0.f, 0.f, 0.f);
				if (level == 3) {
					headScale *= (float)cg_sentry3BodyScale;
					bodyOff.x = (float)cg_sentry3BodyOffX;
					bodyOff.y = (float)cg_sentry3BodyOffY;
					bodyOff.z = (float)cg_sentry3BodyOffZ;
					barrelScale *= (float)cg_sentry3BarrelScale;
					barrelOff.y = (float)cg_sentry3BarrelOffY;
					barrelOff.z = (float)cg_sentry3BarrelOffZ;
				}

				if (headModel) {
					Matrix4 mat =
					  Matrix4::FromAxis(right, fwd, upv, HeadOrigin()) *
					  fix * Matrix4::Translate(bodyOff) *
					  Matrix4::Scale(headScale);
					ModelRenderParam p;
					p.matrix = mat;
					r->RenderModel(*headModel, p);
				}

				// level-3 barrels: mounted on the body (the body
				// seat, then their own seat), rolled around the
				// TRUE barrel axis by the live spin angle. The axis
				// is parallel to the model's forward direction (Y)
				// but passes through the console-tunable
				// cg_sentryBarrelPivot{X,Z} offset (MODEL units,
				// from the kv6 pivot to the barrel axis; y cancels
				// out on a Y-axis spin), applied as T(offset) * R *
				// T(-offset) exactly like the first-person minigun
				// skin. Both seats and the spin pivot ride INSIDE
				// the barrels' scale, so they stay valid whatever
				// cg_sentryScale is.
				if (barrelsModel) {
					Vector3 pivot = MakeVector3(
					  (float)cg_sentryBarrelPivotX, 0.f,
					  (float)cg_sentryBarrelPivotZ);
					Matrix4 barrelMat =
					  Matrix4::FromAxis(right, fwd, upv, HeadOrigin()) *
					  fix * Matrix4::Translate(bodyOff) *
					  Matrix4::Translate(barrelOff) *
					  Matrix4::Scale(barrelScale) *
					  Matrix4::Translate(pivot) *
					  Matrix4::Rotate(MakeVector3(0.f, 1.f, 0.f),
					                  barrelsRoll) *
					  Matrix4::Translate(-pivot);
					ModelRenderParam p;
					p.matrix = barrelMat;
					r->RenderModel(*barrelsModel, p);
				}
			}

			// --- muzzle flash: short warm light after each shot.
			//     cg_sentryFlash 0 disables the light only (the gun
			//     keeps firing tracers and sounds): this is the only
			//     sentry feature the vanilla engine never exercises
			//     at a sustained rate, so keep a live switch for
			//     bisection in case of a freeze ---
			if (flashTimer > 0.f && (float)cg_sentryFlash != 0.f) {
				DynamicLightParam light;
				light.type = DynamicLightTypePoint;
				light.origin = HeadOrigin() +
				  AimForward() *
				  SentryMuzzleDistance(level);
				// v31: keep the flash light glued to the
				// level-3 muzzle trim (same offset as Fire)
				if (level == 3)
					light.origin.z -=
					  (float)cg_sentry3MuzzleZ;
				// v25: wider than the bullets' own light
				// (the vanilla tracer light is a radius-10
				// linear light); default 14, live-tunable
				light.radius = (float)cg_sentryFlashRadius *
				  (flashTimer / FlashTime);
				light.color = MakeVector3(1.0f, 0.8f, 0.5f);
				r->AddLight(light);
			}
		}
	} // namespace client
} // namespace spades