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

#include "Client.h"

#include <Core/ConcurrentDispatch.h>
#include <Core/Settings.h>
#include <Core/Strings.h>

#include "IAudioChunk.h"
#include "IAudioDevice.h"

#include "BloodMarks.h"
#include "CenterMessageView.h"
#include "ChatWindow.h"
#include "ClientPlayer.h"
#include "ClientUI.h"
#include "Corpse.h"
#include "FallingBlock.h"
#include "HurtRingView.h"
#include "ILocalEntity.h"
#include "MapView.h"
#include "PieMenuView.h"
#include "Tracer.h"

#include "GameMap.h"
#include "Grenade.h"
#include "Weapon.h"
#include "World.h"

#include "NetClient.h"

DEFINE_SPADES_SETTING(cg_ragdoll, "1");
SPADES_SETTING(cg_blood);
DEFINE_SPADES_SETTING(cg_ejectBrass, "1");
DEFINE_SPADES_SETTING(cg_hitFeedbackSoundGain, "0.2");
DEFINE_SPADES_SETTING(cg_headshotFeedbackSoundGain, "0.2");
DEFINE_SPADES_SETTING(cg_deathSoundGain, "0.2");
DEFINE_SPADES_SETTING(cg_respawnSoundGain, "1");
DEFINE_SPADES_SETTING(cg_killSounds, "0");
DEFINE_SPADES_SETTING(cg_killSoundsPitch, "1");
DEFINE_SPADES_SETTING(cg_killSoundsGain, "0.2");
DEFINE_SPADES_SETTING(cg_tracers, "1");
DEFINE_SPADES_SETTING(cg_hitLog, "0");
DEFINE_SPADES_SETTING(cg_killfeedIcons, "1");
DEFINE_SPADES_SETTING(cg_killfeedStreaks, "1");
DEFINE_SPADES_SETTING(cg_classicSprinting, "0");
DEFINE_SPADES_SETTING(cg_spectatorNoclip, "0");

SPADES_SETTING(cg_smallFont);
SPADES_SETTING(cg_centerMessage);
SPADES_SETTING(cg_holdAimDownSight);
SPADES_SETTING(cg_damageIndicators);
SPADES_SETTING(cg_corpseDisableGravity);

namespace spades {
	namespace client {

#pragma mark - World States

		float Client::GetSprintState() {
			stmp::optional<ClientPlayer&> p = GetLocalClientPlayer();
			if (!p)
				return 0.0F;
			return p->GetSprintState();
		}

		float Client::GetAimDownState() {
			stmp::optional<ClientPlayer&> p = GetLocalClientPlayer();
			if (!p)
				return 0.0F;
			return p->GetAimDownState();
		}

		bool Client::CanLocalPlayerUseTool() {
			if (!world)
				return false;
			stmp::optional<Player&> p = world->GetLocalPlayer();
			if (!p || !p->IsAlive())
				return false;

			stmp::optional<ClientPlayer&> clientPlayer = GetLocalClientPlayer();

			// Player is unable to use a tool while/soon after sprinting
			if (clientPlayer->GetSprintState() > 0 || p->GetInput().sprint)
				return false;

			// Player is unable to use a tool while switching to another tool
			if (clientPlayer->IsChangingTool())
				return false;

			return true;
		}

		bool Client::CanLocalPlayerUseWeapon() {
			if (!CanLocalPlayerUseTool())
				return false;

			// Player is unable to use weapon while reloading (except shotgun)
			Weapon& weapon = world->GetLocalPlayer()->GetWeapon();
			if (weapon.IsAwaitingReloadCompletion() && !weapon.IsReloadSlow())
				return false;

			// Player runs out of ammo
			if (weapon.GetAmmo() == 0)
				return false;

			return true;
		}

		bool Client::CanLocalPlayerReloadWeapon() {
			Weapon& weapon = world->GetLocalPlayer()->GetWeapon();

			// Playing reload animation or reloading on the server
			if (weapon.IsAwaitingReloadCompletion())
				return false;

			if (weapon.IsClipFull())
				return false; // clip already full
			if (weapon.GetStock() == 0)
				return false; // no stock ammo
			if (weapon.IsShooting() && weapon.GetAmmo() > 0)
				return false; // can't reload while firing

			// Player is unable to reload while switching to another tool
			if (GetLocalClientPlayer()->IsChangingTool())
				return false;

			return true;
		}

		stmp::optional<ClientPlayer&> Client::GetLocalClientPlayer() {
			if (!world || !world->GetLocalPlayerIndex())
				return {};

			return clientPlayers.at(static_cast<std::size_t>(*world->GetLocalPlayerIndex()));
		}

#pragma mark - World Actions

		void Client::SetBlockColor(IntVector3 color) {
			if (!world)
				return;
			stmp::optional<Player&> p = world->GetLocalPlayer();
			if (!p)
				return;
			p->SetHeldBlockColor(color);
			activeNet->SendHeldBlockColor();
		}

		/** Captures the color of the block player is looking at. */
		void Client::CaptureColor() {
			Player& p = world->GetLocalPlayer().value();

			World::WeaponRayCastResult res;
			res = world->WeaponRayCast(p.GetEye(), p.GetFront(), p.GetId());

			IntVector3 col;
			if (res.hit) {
				if (res.playerId) {
					Player& player = world->GetPlayer(res.playerId.value()).value();
					col = player.IsToolBlock() ? player.GetBlockColor() : player.GetColor();
				} else {
					col = IntVectorFromColor(
					  map->GetColorWrapped(res.blockPos.x, res.blockPos.y, res.blockPos.z));
				}
			} else {
				col = world->GetFogColor();
			}

			p.SetHeldBlockColor(col);
			activeNet->SendHeldBlockColor();
		}

		void Client::SetSelectedTool(Player::ToolType type, bool quiet) {
			if (type == world->GetLocalPlayer()->GetTool())
				return;
			lastTool = world->GetLocalPlayer()->GetTool();
			hasLastTool = true;
			hotBarIconState = 1.0F;
			world->GetLocalPlayer()->SetTool(type);

			if (!quiet) {
				Handle<IAudioChunk> c = audioDevice->RegisterSound("Sounds/Weapons/SwitchLocal.opus");
				audioDevice->PlayLocal(c.GetPointerOrNull(), MakeVector3(0.4F, -0.3F, 0.5F), AudioParam());
			}
		}

#pragma mark - World Update

		void Client::UpdateWorld(float dt, float gameplayDt) {
			SPADES_MARK_FUNCTION();

			stmp::optional<Player&> maybePlayer = world->GetLocalPlayer();

			if (maybePlayer) {
				Player& player = maybePlayer.value();

				bool localPlayerIsAlive = player.IsAlive();
				bool localPlayerIsSpectator = player.IsSpectator() || staffSpectating;

				// disable input when UI is open
				if (NeedsAbsoluteMouseCoordinate()) {
					weapInput.primary = false;

					// don't reset player input if chat is open
					if (!AcceptsTextInput()) {
						weapInput.secondary = false;
						playerInput = PlayerInput();
						largeMapView->SetZoom(false);
						chatWindow->SetExpanded(false);
						scoreboardVisible = false;
					}

					// reset all "delayed actions"
					reloadKeyPressed = false;
					debugHitTestZoom = false;
					spectatorZoom = false;
				}

				if (localPlayerIsSpectator) {
					UpdateLocalSpectator(dt);
				} else if (localPlayerIsAlive) {
					UpdateLocalPlayer(dt);
				} else { // update dead
					int count = (int)roundf(player.GetTimeToRespawn());
					if (count != lastRespawnCount) {
						if (count > 0 && count <= 3) {
							Handle<IAudioChunk> c = (count == 1)
								? audioDevice->RegisterSound("Sounds/Feedback/Beep1.opus")
								: audioDevice->RegisterSound("Sounds/Feedback/Beep2.opus");
							AudioParam param;
							param.volume = cg_respawnSoundGain;
							audioDevice->PlayLocal(c.GetPointerOrNull(), param);
						}
						lastRespawnCount = count;
					}
				}

				// handle health updates
				int health = player.GetHealth(); // current health
				if (health != lastHealth) {
					if (health < lastHealth) { // ouch!
						static constexpr std::array<const char*, 4> snds = {
						  "Sounds/Weapons/Impacts/FleshLocal1.opus",
						  "Sounds/Weapons/Impacts/FleshLocal2.opus",
						  "Sounds/Weapons/Impacts/FleshLocal3.opus",
						  "Sounds/Weapons/Impacts/FleshLocal4.opus"
						};
						Handle<IAudioChunk> c = audioDevice->RegisterSound(SampleRandomElement(snds));
						audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());

						float hpper = health / 100.0F;
						int cnt = 18 - (int)(hpper * 8.0F);
						hurtSprites.resize(std::max(cnt, 6));
						for (size_t i = 0; i < hurtSprites.size(); i++) {
							HurtSprite& spr = hurtSprites[i];
							spr.angle = SampleRandomFloat() * M_PI_F * 2.0F;
							spr.scale = 0.2F + SampleRandomFloat() * SampleRandomFloat() * 0.7F;
							spr.horzShift = SampleRandomFloat();
							spr.strength = 0.3F + SampleRandomFloat() * 0.7F;
							if (hpper > 0.5F)
								spr.strength *= 1.5F - hpper;
						}

						if (lastHealth > 0)
							lastHurtTime = time;
						lastHealTime = -100.0F;
						damageTaken = lastHealth - health;
					} else {
						lastHurtTime = -100.0F;
						if (lastHealth > 0)
							lastHealTime = time;
						damageTaken = 0;
					}

					lastHealth = health;
				}

				// handle score updates
				int score = player.GetScore();
				if (score != lastScore)
					lastScore = score;
			} else if (IsDemoMode()) {
				// In demo mode with no local player, still update spectator camera
				UpdateLocalSpectator(dt);
			}

			// Check if demo is paused - freeze game objects but allow camera movement
			bool demoPaused = IsDemoMode() && demoNet && demoNet->IsPaused();

			if (!demoPaused) {
#if 0
				// dynamic time step
				// physics diverges from server
				world->Advance(dt);
#else
				// accurately resembles server's physics
				// but not smooth
				if (gameplayDt > 0.0F) {
					worldSubFrame += gameplayDt;
					worldSubFrameFast += gameplayDt;
				}

				// these run at exactly ~60fps
				float frameStep = 1.0F / 60.0F;
				while (worldSubFrame >= frameStep) {
					world->Advance(frameStep); // physics update
					worldSubFrame -= frameStep;
				}

				// these run at min. ~60fps but as fast as possible
				float step = std::min(gameplayDt, frameStep);
				while (worldSubFrameFast >= step) {
					world->UpdatePlayer(step, false); // smooth orientation update
					worldSubFrameFast -= step;
				}
#endif

				// update player view (doesn't affect physics/game logics)
				for (const auto& clientPlayer : clientPlayers) {
					if (clientPlayer)
						clientPlayer->Update(gameplayDt);
				}

				// corpse never accesses audio nor renderer, so
				// we can do it in the separate thread
				class CorpseUpdateDispatch : public ConcurrentDispatch {
					Client& client;
					float dt;

				public:
					CorpseUpdateDispatch(Client& c, float dt) : client{c}, dt{dt} {}
					void Run() override {
						for (const auto& c : client.corpses) {
							for (int i = 0; i < 4; i++)
								c->Update(dt / 4.0F);
						}
					}
				};
				CorpseUpdateDispatch corpseDispatch{*this, gameplayDt};
				corpseDispatch.Start();

				// local entities should be done in the client thread
				{
					decltype(localEntities)::iterator it;
					std::vector<decltype(it)> its;
					for (it = localEntities.begin(); it != localEntities.end(); it++) {
						if (!(*it)->Update(gameplayDt))
							its.push_back(it);
					}
					for (const auto& it : its)
						localEntities.erase(it);
				}

				bloodMarks->Update(gameplayDt);
				corpseDispatch.Join();
			}

			if (grenadeVibration > 0.0F) {
				grenadeVibration -= dt;
				if (grenadeVibration < 0.0F)
					grenadeVibration = 0.0F;
			}

			if (grenadeVibrationSlow > 0.0F) {
				grenadeVibrationSlow -= dt;
				if (grenadeVibrationSlow < 0.0F)
					grenadeVibrationSlow = 0.0F;
			}

			if (hotBarIconState > 0.0F) {
				hotBarIconState -= dt * 10.0F;
				if (hotBarIconState < 0.0F)
					hotBarIconState = 0.0F;
			}

			if (hitFeedbackIconState > 0.0F) {
				hitFeedbackIconState -= dt * 4.0F;
				if (hitFeedbackIconState < 0.0F)
					hitFeedbackIconState = 0.0F;
			}

			if (debugHitTestZoom) {
				debugHitTestZoomState += dt * 5.0F;
				if (debugHitTestZoomState > 1.0F)
					debugHitTestZoomState = 1.0F;
			} else {
				debugHitTestZoomState -= dt * 5.0F;
				if (debugHitTestZoomState < 0.0F)
					debugHitTestZoomState = 0.0F;
			}

			if (spectatorZoom) {
				spectatorZoomState += dt * 3.0F;
				if (spectatorZoomState > 1.0F)
					spectatorZoomState = 1.0F;
			} else {
				spectatorZoomState -= dt * 5.0F;
				if (spectatorZoomState < 0.0F)
					spectatorZoomState = 0.0F;
			}
		}

		/** Handles movement of spectating local player. */
		void Client::UpdateLocalSpectator(float dt) {
			SPADES_MARK_FUNCTION();

			if (GetCameraMode() != ClientCameraMode::Free)
				return;

			auto& freeState = freeCameraState;

			Vector3 lastPos = freeState.position;
			freeState.velocity *= powf(0.3F, dt);
			freeState.position += freeState.velocity * dt;

			if (freeState.position.x < 0.0F)
				freeState.velocity.x = fabsf(freeState.velocity.x) * 0.2F;
			if (freeState.position.y < 0.0F)
				freeState.velocity.y = fabsf(freeState.velocity.y) * 0.2F;
			if (freeState.position.x > static_cast<float>(map->Width()))
				freeState.velocity.x = fabsf(freeState.velocity.x) * -0.2F;
			if (freeState.position.y > static_cast<float>(map->Height()))
				freeState.velocity.y = fabsf(freeState.velocity.y) * -0.2F;
			if (freeState.position.z > static_cast<float>(map->Depth()))
				freeState.velocity.z = fabsf(freeState.velocity.z) * -0.2F;

			freeState.position = lastPos + freeState.velocity * dt;

			// check collision
			if (!cg_spectatorNoclip) {
				GameMap::RayCastResult minResult{};
				float minDist = 1.0E+10F;
				Vector3 minShift = MakeVector3(0.0F, 0.0F, 0.0F);

				const Vector3 dir = freeState.position - lastPos;
				const Vector3 normDir = dir.Normalize();

				if (freeState.velocity.GetSquaredLength() < 0.01F) {
					freeState.position = lastPos;
					freeState.velocity *= 0.0F;
				} else {
					for (int sx = -1; sx <= 1; sx++)
					for (int sy = -1; sy <= 1; sy++)
					for (int sz = -1; sz <= 1; sz++) {
						Vector3 shift = {sx * 0.1F, sy * 0.1F, sz * 0.1F};

						GameMap::RayCastResult res = map->CastRay2(lastPos + shift, normDir, 16);
						if (res.hit == false || res.startSolid)
							continue;

						Vector3 hitPos = res.hitPos - freeState.position - shift;
						if (Vector3::Dot(hitPos, dir) >= 0.0F)
							continue;

						float dist = Vector3::Dot(hitPos, normDir);
						if (dist < minDist) {
							minResult = res;
							minDist = dist;
							minShift = shift;
						}
					}
				}

				if (minDist < 1.0E+9F) {
					Vector3 hitPos = minResult.hitPos - minShift;
					Vector3 normal = MakeVector3(minResult.normal);
					freeState.position = hitPos + (normal * 0.02F);

					// bounce
					float dot = Vector3::Dot(freeState.velocity, normal);
					freeState.velocity -= normal * (dot * 1.2F);
				}
			}

			// acceleration
			Vector3 front;
			Vector3 up = {0, 0, -1};

			auto& sharedState = followAndFreeCameraState;
			front.x = cosf(sharedState.pitch) * -cosf(sharedState.yaw);
			front.y = cosf(sharedState.pitch) * -sinf(sharedState.yaw);
			front.z = sinf(sharedState.pitch);

			Vector3 right = -Vector3::Cross(up, front).Normalize();
			up = Vector3::Cross(right, front).Normalize();

			float f = dt * 32.0F;
			if (playerInput.sprint)
				f *= 3.0F;
			else if (playerInput.sneak)
				f *= 0.5F;

			front *= f;
			right *= f;
			up *= f;

			if (playerInput.moveForward)
				freeState.velocity += front;
			else if (playerInput.moveBackward)
				freeState.velocity -= front;

			if (playerInput.moveLeft)
				freeState.velocity -= right;
			else if (playerInput.moveRight)
				freeState.velocity += right;

			if (playerInput.jump)
				freeState.velocity += up;
			else if (playerInput.crouch)
				freeState.velocity -= up;

			SPAssert(freeState.velocity.GetLength() < 100.0F);
		}

		/** Handles movement of joined local player. */
		void Client::UpdateLocalPlayer(float dt) {
			SPADES_MARK_FUNCTION();

			Player& player = world->GetLocalPlayer().value();
			Weapon& weapon = player.GetWeapon();

			PlayerInput inp = playerInput;
			WeaponInput winp = weapInput;

			// suppress weapon input while pie menu is held
			if (pieMenuView && pieMenuView->IsOpen())
				winp = WeaponInput();

			bool isToolWeapon = player.IsToolWeapon();
			bool isWeaponShotgun = weapon.IsReloadSlow();

			// stop sprinting if player is moving too slow
			float vel2D = player.GetVelocity().GetSquaredLength2D();
			if (vel2D < 0.01F && !cg_classicSprinting)
				inp.sprint = false;

			// stop sprinting when crouching or sneaking
			if (inp.crouch || inp.sneak)
				inp.sprint = false;

			// don't allow jumping in the air
			if (inp.jump && !player.IsOnGroundOrWade())
				inp.jump = false;

			// can't use a tool while sprinting or switching to another tool, etc.
			if (!CanLocalPlayerUseTool()) {
				winp.primary = false;
				winp.secondary = false;
				player.SetBlockCursorDragging(false);
			}

			if (isToolWeapon) {
				// disable firing if the player is out of ammo
				if (weapon.GetAmmo() == 0)
					winp.primary = false;

				// disable weapon while reloading (except for shotgun)
				if (weapon.IsAwaitingReloadCompletion() && !isWeaponShotgun) {
					winp.primary = false;
					winp.secondary = false;
				}
			}

			player.SetInput(inp);
			player.SetWeaponInput(winp);

			PlayerInput actualInput = player.GetInput();
			WeaponInput actualWeapInput = player.GetWeaponInput();

			// Uncrouching may be prevented by an obstacle
			inp.crouch = actualInput.crouch;

			// send player input
			{
				PlayerInput sentInput = inp;
				WeaponInput sentWeaponInput = winp;

				activeNet->SendPlayerInput(sentInput);
				activeNet->SendWeaponInput(sentWeaponInput);
			}

			// send weapon reload
			if (isToolWeapon && CanLocalPlayerReloadWeapon() && reloadKeyPressed) {
				// disable zoom while reloading (except for shotgun)
				if (winp.secondary && !isWeaponShotgun) {
					winp.secondary = false;
					player.SetWeaponInput(winp);
					activeNet->SendWeaponInput(winp);
					actualWeapInput = winp;
					// do not overwrite input so zoom resumes automatically
					if (!cg_holdAimDownSight)
						weapInput.secondary = winp.secondary;
				}

				weapon.Reload();
				activeNet->SendReload();
			}

			// is the selected tool no longer usable (ex. out of ammo)?
			if (!player.IsToolSelectable(player.GetTool())) {
				// release mouse buttons before auto-switching tools
				winp.primary = false;
				winp.secondary = false;
				player.SetWeaponInput(winp);
				activeNet->SendWeaponInput(winp);
				actualWeapInput = weapInput = winp;

				// select another tool
				Player::ToolType t = player.GetTool();
				do {
					switch (t) {
						case Player::ToolSpade: t = Player::ToolGrenade; break;
						case Player::ToolBlock: t = Player::ToolSpade; break;
						case Player::ToolWeapon: t = Player::ToolBlock; break;
						case Player::ToolGrenade: t = Player::ToolWeapon; break;
					}
				} while (!player.IsToolSelectable(t));
				SetSelectedTool(t);
			}

			// send position/orientaion updates
			{
				float POSITION_UPDATE_RATE = 1.0F;             // 1/s
				float ORIENTATION_UPDATE_RATE = 1.0F / 120.0F; // 120/s

				Vector3 curPos = player.GetPosition();
				if (curPos != lastPosSent
					&& time - lastPosSentTime > POSITION_UPDATE_RATE) {
					lastPosSentTime = time;
					lastPosSent = curPos;
					activeNet->SendPosition(curPos);
				}

				Vector3 curFront = player.GetFront();
				if (curFront != lastFrontSent
					&& time - lastOriSentTime > ORIENTATION_UPDATE_RATE) {
					lastOriSentTime = time;
					lastFrontSent = curFront;
					activeNet->SendOrientation(curFront);
				}
			}

			// show block count when building block lines.
			if (player.IsToolBlock() && player.IsBlockCursorDragging()) {
				if (player.IsBlockCursorActive()) {
					int blocks = world->CubeLineCount(player.GetBlockCursorDragPos(),
													  player.GetBlockCursorPos());
					auto msg = _TrN("Client", "{0} block", "{0} blocks", blocks);
					auto type = (blocks > player.GetNumBlocks())
						? AlertType::Warning : AlertType::Notice;
					ShowAlert(msg, type, 0.0F, true);
				} else { // invalid
					auto msg = _Tr("Client", "-- blocks");
					ShowAlert(msg, AlertType::Warning, 0.0F, true);
				}
			}
		}

#pragma mark - IWorldListener Handlers

		void Client::PlayerObjectSet(int id) {
			if (clientPlayers[id])
				clientPlayers[id] = nullptr;

			stmp::optional<Player&> p = world->GetPlayer(id);
			if (p)
				clientPlayers[id] = Handle<ClientPlayer>::New(*p, *this);
		}

		void Client::PlayerJumped(spades::client::Player& p) {
			SPADES_MARK_FUNCTION();

			if (!IsMuted()) {
				Handle<IAudioChunk> c = p.GetWade()
					? audioDevice->RegisterSound("Sounds/Player/WaterJump.opus")
					: audioDevice->RegisterSound("Sounds/Player/Jump.opus");
				audioDevice->Play(c.GetPointerOrNull(), p.GetOrigin(), AudioParam());
			}
		}

		void Client::PlayerLanded(spades::client::Player& p, bool hurt) {
			SPADES_MARK_FUNCTION();

			if (!IsMuted()) {
				Handle<IAudioChunk> c = hurt
					? audioDevice->RegisterSound("Sounds/Player/FallHurt.opus")
					: (p.GetWade() ? audioDevice->RegisterSound("Sounds/Player/WaterLand.opus")
						: audioDevice->RegisterSound("Sounds/Player/Land.opus"));
				audioDevice->Play(c.GetPointerOrNull(), p.GetOrigin(), AudioParam());
			}
		}

		void Client::PlayerMadeFootstep(spades::client::Player& p) {
			SPADES_MARK_FUNCTION();

			static constexpr std::array<const char*, 4> snds = {
			  "Sounds/Player/Footstep1.opus", "Sounds/Player/Footstep2.opus",
			  "Sounds/Player/Footstep3.opus", "Sounds/Player/Footstep4.opus"
			};
			static constexpr std::array<const char*, 4> wsnds = {
			  "Sounds/Player/Wade1.opus", "Sounds/Player/Wade2.opus",
			  "Sounds/Player/Wade3.opus", "Sounds/Player/Wade4.opus"
			};
			static constexpr std::array<const char*, 6> rsnds = {
			  "Sounds/Player/Run1.opus", "Sounds/Player/Run2.opus", "Sounds/Player/Run3.opus",
			  "Sounds/Player/Run4.opus", "Sounds/Player/Run5.opus", "Sounds/Player/Run6.opus"
			};

			if (!IsMuted()) {
				float sprintState = clientPlayers[p.GetId()]
					? clientPlayers[p.GetId()]->GetSprintState() : 0.0F;

				Handle<IAudioChunk> c =
				  audioDevice->RegisterSound(SampleRandomElement(p.GetWade() ? wsnds : snds));
				audioDevice->Play(c.GetPointerOrNull(), p.GetOrigin(), AudioParam());

				if (sprintState > 0.5F && !p.GetWade()) {
					AudioParam param;
					param.volume *= sprintState;
					c = audioDevice->RegisterSound(SampleRandomElement(rsnds));
					audioDevice->Play(c.GetPointerOrNull(), p.GetOrigin(), param);
				}
			}
		}

		void Client::PlayerFiredWeapon(spades::client::Player& p) {
			SPADES_MARK_FUNCTION();

			if (IsInFirstPersonView(p.GetId()))
				localFireVibrationTime = time;

			clientPlayers.at(p.GetId())->FiredWeapon();
		}

		void Client::PlayerEjectedBrass(spades::client::Player& p) {
			SPADES_MARK_FUNCTION();

			if (!cg_ejectBrass)
				return;

			clientPlayers.at(p.GetId())->EjectedBrass();
		}

		void Client::PlayerDryFiredWeapon(spades::client::Player& p) {
			SPADES_MARK_FUNCTION();

			if (!IsMuted()) {
				Handle<IAudioChunk> c = audioDevice->RegisterSound("Sounds/Weapons/DryFire.opus");
				if (p.IsLocalPlayer())
					audioDevice->PlayLocal(c.GetPointerOrNull(), MakeVector3(0.4F, -0.3F, 0.5F),
										   AudioParam());
				else
					audioDevice->Play(c.GetPointerOrNull(),
									  p.GetEye() + p.GetFront() * 0.5F - p.GetUp() * 0.3F +
										p.GetRight() * 0.4F,
									  AudioParam());
			}
		}

		void Client::PlayerReloadingWeapon(spades::client::Player& p) {
			SPADES_MARK_FUNCTION();

			clientPlayers.at(p.GetId())->ReloadingWeapon();
		}

		void Client::PlayerReloadedWeapon(spades::client::Player& p) {
			SPADES_MARK_FUNCTION();

			clientPlayers.at(p.GetId())->ReloadedWeapon();
		}

		void Client::PlayerChangedTool(spades::client::Player& p) {
			SPADES_MARK_FUNCTION();

			if (p.IsLocalPlayer())
				return; // played by ClientPlayer::Update

			if (!IsMuted()) {
				Handle<IAudioChunk> c = audioDevice->RegisterSound("Sounds/Weapons/Switch.opus");
				audioDevice->Play(c.GetPointerOrNull(),
								  p.GetEye() + p.GetFront() * 0.5F - p.GetUp() * 0.3F +
									p.GetRight() * 0.4F,
								  AudioParam());
			}
		}

		void Client::PlayerRestocked(spades::client::Player& p) {
			if (!IsMuted()) {
				Handle<IAudioChunk> c;
				if (IsInFirstPersonView(p.GetId())) {
					c = audioDevice->RegisterSound("Sounds/Weapons/RestockLocal.opus");
					audioDevice->PlayLocal(c.GetPointerOrNull(),
						MakeVector3(0.4F, -0.3F, 0.5F), AudioParam());
				} else {
					c = audioDevice->RegisterSound("Sounds/Weapons/Restock.opus");
					audioDevice->Play(c.GetPointerOrNull(),
						p.GetEye() + p.GetFront() * 0.5F - p.GetUp() * 0.3F +
						p.GetRight() * 0.4F, AudioParam());
				}
			}
		}

		void Client::PlayerPulledGrenadePin(spades::client::Player& p) {
			SPADES_MARK_FUNCTION();

			if (!IsMuted()) {
				Handle<IAudioChunk> c =
				  audioDevice->RegisterSound("Sounds/Weapons/Grenade/Fire.opus");
				if (IsInFirstPersonView(p.GetId())) {
					audioDevice->PlayLocal(c.GetPointerOrNull(),
						MakeVector3(0.4F, -0.3F, 0.5F), AudioParam());
				} else {
					audioDevice->Play(c.GetPointerOrNull(),
						p.GetEye() + p.GetFront() * 0.9F - p.GetUp() * 0.2F +
						p.GetRight() * 0.3F, AudioParam());
				}
			}
		}

		void Client::PlayerThrewGrenade(spades::client::Player& p,
			stmp::optional<const Grenade&> g) {
			SPADES_MARK_FUNCTION();

			if (g && p.IsLocalPlayer())
				activeNet->SendGrenade(*g);

			if (!IsMuted()) {
				Handle<IAudioChunk> c =
				  audioDevice->RegisterSound("Sounds/Weapons/Grenade/Throw.opus");
				if (IsInFirstPersonView(p.GetId())) {
					audioDevice->PlayLocal(c.GetPointerOrNull(),
						MakeVector3(0.4F, 0.1F, 0.3F), AudioParam());
				} else {
					audioDevice->Play(c.GetPointerOrNull(),
						p.GetEye() + p.GetFront() * 0.9F - p.GetUp() * 0.2F +
						p.GetRight() * 0.3F, AudioParam());
				}
			}
		}

		void Client::PlayerMissedSpade(spades::client::Player& p) {
			SPADES_MARK_FUNCTION();

			if (!IsMuted()) {
				Handle<IAudioChunk> c =
				  audioDevice->RegisterSound("Sounds/Weapons/Spade/Miss.opus");
				if (IsInFirstPersonView(p.GetId())) {
					audioDevice->PlayLocal(c.GetPointerOrNull(),
						MakeVector3(0.2F, -0.1F, 0.7F), AudioParam());
				} else {
					audioDevice->Play(c.GetPointerOrNull(),
						p.GetOrigin() + p.GetFront() * 0.9F +
						p.GetUp() * 1.25F, AudioParam());
				}
			}
		}

		void Client::PlayerHitBlockWithSpade(spades::client::Player& p, Vector3 hitPos,
											 IntVector3 blockPos, IntVector3 normal) {
			SPADES_MARK_FUNCTION();

			if (IsInFirstPersonView(p.GetId()))
				localFireVibrationTime = time;

			if (blockPos.z >= 63) {
				Vector3 shiftedHitPos = hitPos + (MakeVector3(normal) * 0.05F);
				BulletHitWaterSurface(shiftedHitPos);

				// TODO: use a better sound
				if (!IsMuted()) {
					Handle<IAudioChunk> c =
					  audioDevice->RegisterSound("Sounds/Weapons/Impacts/Water2.opus");
					AudioParam param;
					param.volume = 2.0F;
					param.pitch = 0.9F + SampleRandomFloat() * 0.2F;
					audioDevice->Play(c.GetPointerOrNull(), shiftedHitPos, param);
				}
			} else {
				Vector3 origin = MakeVector3(blockPos) + 0.5F;
				Vector3 shiftedHitPos = origin + (MakeVector3(normal) * 0.6F);

				uint32_t col = map->GetColor(blockPos.x, blockPos.y, blockPos.z);

				// apply block darkening
				int health = col >> 24;
				uint32_t f = (std::max(health, 32) << 8) / 100;
				col = DarkenColor(col, f);

				col = map->GetColorJit(col); // randomize color
				EmitBlockFragments(shiftedHitPos, IntVectorFromColor(col));

				if (!IsMuted()) {
					Handle<IAudioChunk> c =
					  audioDevice->RegisterSound("Sounds/Weapons/Spade/HitBlock.opus");
					audioDevice->Play(c.GetPointerOrNull(), shiftedHitPos, AudioParam());
				}
			}
		}

		void Client::PlayerKilledPlayer(spades::client::Player& killer,
			spades::client::Player& victim, KillType kt) {

			// only used in case of KillTypeWeapon
			const auto& weaponType = killer.GetWeapon().GetWeaponType();

			const int victimId = victim.GetId();
			const int killerId = killer.GetId();

			bool isRevengeKill = false;
			if (killerId != victimId && (killer.IsLocalPlayer() || victim.IsLocalPlayer())) {
				// check if the victim was dominating before dying
				if (killStreaks[victimId][killerId] >= 4) {
					killStreaks[killerId][victimId] = 1; // start new streak for the killer
					isRevengeKill = true;
				} else {
					killStreaks[killerId][victimId]++; // increment killer streak
				}
				killStreaks[victimId][killerId] = 0; // reset victim streak
			}

			// The local player is dead
			if (victim.IsLocalPlayer()) {
				lastAliveTime = time;

				// initialize the look-you-are-dead camera
				if (followCameraState.enabled && !staffSpectating) {
					Vector3 o = -victim.GetFront();
					followAndFreeCameraState.yaw = atan2f(o.y, o.x);
					followAndFreeCameraState.pitch = DEG2RAD(30);

					// reset follow camera state
					followCameraState.firstPerson = true;
					followCameraState.enabled = false;
				}

				// play death sound
				Handle<IAudioChunk> c = audioDevice->RegisterSound("Sounds/Player/Death.opus");
				AudioParam param;
				param.volume = cg_deathSoundGain;
				audioDevice->PlayLocal(c.GetPointerOrNull(), param);

				// reset hit counter
				if (kt == KillTypeClassChange)
					hitStats = HitStats();

				// register local deaths
				curDeaths++;
				if (curStreak > bestStreak)
					bestStreak = curStreak;
				curStreak = 0;
			} else {
				// play hit sound for non local player: see BullethitPlayer
				if (!IsMuted() && (kt == KillTypeWeapon || kt == KillTypeHeadshot)) {
					static constexpr std::array<const char*, 3> snds = {
					  "Sounds/Weapons/Impacts/Flesh1.opus",
					  "Sounds/Weapons/Impacts/Flesh2.opus",
					  "Sounds/Weapons/Impacts/Flesh3.opus"
					};
					Handle<IAudioChunk> c = audioDevice->RegisterSound(SampleRandomElement(snds));

					AudioParam param;
					param.volume = 4.0F;
					audioDevice->Play(c.GetPointerOrNull(), victim.GetEye(), param);
				}

				// register local kills
				if (killer.IsLocalPlayer()) {
					curKills++;
					curStreak++;
					if (kt == KillTypeMelee)
						meleeKills++;
					else if (kt == KillTypeGrenade)
						grenadeKills++;

					if (cg_killSounds && curStreak >= 2) {
						AudioParam param;
						param.pitch = (float)cg_killSoundsPitch;
						param.volume = (float)cg_killSoundsGain;

						int sndIndex = curStreak - 2;
						if (sndIndex < static_cast<int>(killSounds.size())) {
							Handle<IAudioChunk> c = killSounds[sndIndex];
							audioDevice->PlayLocal(c.GetPointerOrNull(), param);
						}
					}
				}
			}

			// create a killfeed message
			std::string s, cause;

			bool killfeedIcons = cg_killfeedIcons && !cg_smallFont;

			// add colored killer name
			s += ChatWindow::TeamColorMessage(killer.GetName(), killer.GetTeamId());

			switch (kt) {
				case KillTypeWeapon:
					switch (weaponType) {
						case RIFLE_WEAPON: cause += _Tr("Client", "Rifle"); break;
						case SMG_WEAPON: cause += _Tr("Client", "SMG"); break;
						case SHOTGUN_WEAPON: cause += _Tr("Client", "Shotgun"); break;
						default: SPUnreachable();
					}
					break;
				case KillTypeFall: cause += _Tr("Client", "Fall"); break;
				case KillTypeMelee: cause += _Tr("Client", "Melee"); break;
				case KillTypeGrenade: cause += _Tr("Client", "Grenade"); break;
				case KillTypeHeadshot: cause += _Tr("Client", "Headshot"); break;
				case KillTypeTeamChange: cause += _Tr("Client", "Team Change"); break;
				case KillTypeClassChange: cause += _Tr("Client", "Weapon Change"); break;
				default: cause += "???"; break;
			}

			// add cause of death
			s += " ";
			if (killfeedIcons) {
				std::string killImg;
				if (killerId != victimId && (kt == KillTypeWeapon || kt == KillTypeHeadshot)) {
					if (!killer.IsOnGroundOrWade()) // air shots
						killImg += ChatWindow::KillImage(7);
					killImg += ChatWindow::KillImage(KillTypeWeapon, weaponType);
					if (!killer.GetWeaponInput().secondary) // nonscoped shots
						killImg += ChatWindow::KillImage(8);
					if (kt == KillTypeHeadshot)
						killImg += ChatWindow::KillImage(KillTypeHeadshot);
				} else {
					killImg += ChatWindow::KillImage(kt, weaponType);
				}

				if (killerId != victimId && killer.IsTeammate(victim))
					s += ChatWindow::ColoredMessage(killImg, MsgColorFriendlyFire);
				else
					s += killImg;
			} else {
				s += "[";
				if (killerId != victimId && killer.IsTeammate(victim))
					s += ChatWindow::ColoredMessage(cause, MsgColorFriendlyFire);
				else if (killer.IsLocalPlayer() || victim.IsLocalPlayer())
					s += ChatWindow::ColoredMessage(cause, MsgColorGray);
				else
					s += cause;
				s += "]";
			}
			s += " ";

			if (killerId != victimId) {
				// add colored victim name
				s += ChatWindow::TeamColorMessage(victim.GetName(), victim.GetTeamId());

				// add domination indicator
				if (cg_killfeedStreaks && (killer.IsLocalPlayer() || victim.IsLocalPlayer())) {
					const int killerStreak = killStreaks[killerId][victimId];
					if (isRevengeKill || killerStreak >= 2) {
						s += " ";
						if (isRevengeKill) {
							if (killfeedIcons) {
								s += ChatWindow::KillImage(10);
							} else {
								s += "(";
								s += ChatWindow::ColoredMessage(_Tr("Client", "Revenge!"), MsgColorYellow);
								s += ")";
							}
						} else if (killerStreak >= 4) {
							if (killfeedIcons) {
								s += ChatWindow::KillImage(9);
							} else {
								s += "(";
								s += ChatWindow::ColoredMessage(_Tr("Client", "Dominating!"), MsgColorYellow);
								s += ")";
							}
						} else {
							s += "(x" + ToString(killerStreak) + ")";
						}
					}
				}
			}

			killfeedWindow->AddMessage(s);

			// log to netlog
			if (killerId != victimId) {
				NetLog("%s (%s) [%s] %s (%s)", killer.GetName().c_str(),
					   killer.GetTeamName().c_str(), cause.c_str(),
					   victim.GetName().c_str(), victim.GetTeamName().c_str());
			} else {
				NetLog("%s (%s) [%s]", killer.GetName().c_str(),
					killer.GetTeamName().c_str(), cause.c_str());
			}

			// show big message if local player is involved
			if (killerId != victimId && (killer.IsLocalPlayer() || victim.IsLocalPlayer())) {
				std::string msg = "";
				if (victim.IsLocalPlayer()) {
					msg = _Tr("Client", "You were killed by {0}", killer.GetName());
				} else if (cg_centerMessage == 2) {
					msg = _Tr("Client", "You have killed {0}", victim.GetName());
				}

				if (!msg.empty())
					centerMessageView->AddMessage(msg);
			}

			// don't spawn blood/ragdolls for spectators
			if (victim.IsSpectator())
				return;

			// emit blood (also for local player)
			// FIXME: emiting blood for either
			// client-side or server-side hit?
			switch (kt) {
				case KillTypeGrenade:
				case KillTypeHeadshot:
				case KillTypeMelee:
				case KillTypeWeapon: Bleed(victim.GetEye()); break;
				default: break;
			}

			// create ragdoll corpse
			if (cg_ragdoll) {
				auto corp = stmp::make_unique<Corpse>(*renderer, *map, victim);

				if (kt == KillTypeGrenade) {
					corp->AddImpulse(MakeVector3(0, 0, -4.0F - SampleRandomFloat() * 4.0F));
				} else if (killerId != victimId) {
					Vector3 dir = victim.GetPosition() - killer.GetPosition();
					dir = dir.Normalize();

					if (kt == KillTypeMelee) {
						dir *= 6.0F;
					} else {
						switch (weaponType) {
							case SMG_WEAPON: dir *= 2.8F; break;
							case SHOTGUN_WEAPON: dir *= 4.5F; break;
							default: dir *= 3.5F; break;
						}
					}

					// add extra head impulse if its a headshot or melee kill
					if (kt == KillTypeHeadshot || kt == KillTypeMelee)
						corp->AddHeadImpulse(dir * 4.0F);

					if (!victim.IsLocalPlayer() && cg_corpseDisableGravity) {
						dir *= 2.0F;
						corp->AddImpulse(MakeVector3(0, 0, -2.0F));
					}

					corp->AddImpulse(dir);
				}
				corp->AddImpulse(victim.GetVelocity() * 32.0F);

				if (victim.IsLocalPlayer())
					lastLocalCorpse = corp.get();

				corpses.emplace_back(std::move(corp));
				RemoveCorpses();
			}
		}

		void Client::BulletHitPlayer(spades::client::Player& hurtPlayer, HitType type,
									 spades::Vector3 hitPos, spades::client::Player& by,
									 std::unique_ptr<IBulletHitScanState>& stateCell) {
			SPADES_MARK_FUNCTION();

			SPAssert(type != HitTypeBlock);

			const bool byLocalPlayer = by.IsLocalPlayer();
			const bool isMeleeHit = type == HitTypeMelee;

			// spatter blood
			if ((int)cg_blood >= 2) {
				Vector3 dir = by.GetEye() - hitPos;
				float distSqr = dir.GetSquaredLength();
				dir = dir.Normalize();

				float frontSpeed = 8.0F;
				float backSpeed = 0.0F;

				if (isMeleeHit) { // Blunt
					frontSpeed = 1.5F;
				} else {
					switch (by.GetWeapon().GetWeaponType()) {
						case RIFLE_WEAPON: // Penetrating
							frontSpeed = 1.0F;
							backSpeed = 21.0F;
							break;
						case SMG_WEAPON: { // Penetrating
							float rnd = 20.0F * SampleRandomFloat();
							if (distSqr < rnd * rnd) {
								frontSpeed = 1.0F;
								backSpeed = 12.0F;
							}
						} break;
						default: break;
					}
				}

				if (frontSpeed > 0.0F)
					bloodMarks->Spatter(hitPos, dir * frontSpeed, byLocalPlayer);
				if (backSpeed > 0.0F)
					bloodMarks->Spatter(hitPos, dir * -backSpeed, byLocalPlayer);
			}

			// don't spawn blood when in firstperson view
			int focusedPlayerId = GetCameraTargetPlayerId();
			if (!IsFirstPerson(GetCameraMode()) || focusedPlayerId != hurtPlayer.GetId())
				Bleed(hitPos, focusedPlayerId == by.GetId());

			if (hurtPlayer.IsLocalPlayer()) {
				// don't play hit sound now;
				// local bullet impact sound is
				// played by checking the decrease of HP
				return;
			}

			// This function gets called for each pellet. We want to play these sounds no more than
			// once for each instance of firing. `BulletHitScanState`, stored in `stateCell`, tells
			// whether we have played each sound for the current firing session.
			struct BulletHitScanState : IBulletHitScanState {
				bool hasPlayedNormalHitSound = false;
				bool hasPlayedHeadshotSound = false;
				 std::unordered_map<int, DamageIndicator*> indicatorByPlayer;
			};

			if (!stateCell)
				stateCell = stmp::make_unique<BulletHitScanState>();

			auto& hitScanState = dynamic_cast<BulletHitScanState&>(*stateCell);

			if (!hitScanState.hasPlayedNormalHitSound) {
				Handle<IAudioChunk> c;
				AudioParam param;

				if (!IsMuted()) {
					if (isMeleeHit) {
						c = audioDevice->RegisterSound("Sounds/Weapons/Spade/HitPlayer.opus");
						audioDevice->Play(c.GetPointerOrNull(), hitPos, param);
					} else {
						static constexpr std::array<const char*, 3> snds = {
						  "Sounds/Weapons/Impacts/Flesh1.opus",
						  "Sounds/Weapons/Impacts/Flesh2.opus",
						  "Sounds/Weapons/Impacts/Flesh3.opus"
						};
						c = audioDevice->RegisterSound(SampleRandomElement(snds));
						param.volume = 4.0F;
						audioDevice->Play(c.GetPointerOrNull(), hitPos, param);
					}
				}

				if (byLocalPlayer && type != HitTypeHead) {
					c = audioDevice->RegisterSound("Sounds/Feedback/HitFeedback.opus");
					param.volume = cg_hitFeedbackSoundGain;
					audioDevice->PlayLocal(c.GetPointerOrNull(), param);
				}

				hitScanState.hasPlayedNormalHitSound = true;
			}

			if (byLocalPlayer) {
				activeNet->SendHit(hurtPlayer.GetId(), type);

				// register bullet hits
				if (!isMeleeHit)
					weaponStats.hits[by.GetWeaponType()]++;

				if ((bool)cg_damageIndicators && !isMeleeHit) {
					int dmg = by.GetWeapon().GetDamage(type);
					auto& indicator = hitScanState.indicatorByPlayer[hurtPlayer.GetId()];
					if (indicator) {
						indicator->damage += dmg;
						if (!indicator->crit && indicator->damage >= 100) {
							indicator->crit = true;
							indicator->velocity.x = 0.0F;
							indicator->velocity.y = 0.0F;
							indicator->velocity.z = -2.0F;
						}
						indicator->fade = indicator->crit ? 2.0F : 1.5F;
						indicator->lastHitTime = time;
					} else {
						DamageIndicator damages;
						damages.damage = dmg;
						damages.playerId = hurtPlayer.GetId();
						damages.position = hitPos;
						damages.crit = dmg >= 100;
						if (damages.crit) {
							damages.velocity.x = 0.0F;
							damages.velocity.y = 0.0F;
						} else {
							damages.velocity = RandomVector() * 4.0F;
						}
						damages.velocity.z = -2.0F;
						damages.fade = damages.crit ? 2.0F : 1.5F;
						damages.lastHitTime = time;

						damageIndicators.push_back(damages);
						indicator = &damageIndicators.back();
					}
				}

				if ((bool)cg_hitLog) {
					char buf[32];
					std::string str;

					// add hurt player name and distance
					float dist = (by.GetEye() - hurtPlayer.GetEye()).GetLength();
					snprintf(buf, sizeof(buf), "%.1f", dist);
					str += _Tr("Client", "You hit {0} from: {1} blocks ",
						hurtPlayer.GetName(), std::string(buf));

					// add delta time since last hit
					if ((int)cg_hitLog >= 2) {
						float dt = world->GetTime() - lastHitTime;
						if (dt <= 0.0F) {
							str += "dT: NA ";
						} else {
							if (dt > 1.0F)
								snprintf(buf, sizeof(buf), "dT: %.0fs ", dt);
							else
								snprintf(buf, sizeof(buf), "dT: %dms ", (int)(dt * 1000));
							str += buf;
						}
					}

					// add body part
					str += "[";
					switch (type) {
						case HitTypeTorso: str += _Tr("Client", "TORSO"); break;
						case HitTypeArms: str += _Tr("Client", "ARMS"); break;
						case HitTypeLegs: str += _Tr("Client", "LEGS"); break;
						case HitTypeMelee: str += _Tr("Client", "MELEE"); break;
						default: str += _Tr("Client", "HEAD"); break;
					}
					str += "]";

					// add hit count
					if (!isMeleeHit) {
						int hits = 0;
						switch (type) {
							case HitTypeTorso: hits = ++hitStats.numBodyHits; break;
							case HitTypeHead: hits = ++hitStats.numHeadHits; break;
							case HitTypeArms:
							case HitTypeLegs: hits = ++hitStats.numLimbHits; break;
							default: break;
						}
						str += " (" + ToString(hits) + ")";
					}

					scriptedUI->RecordChatLog(str);
					chatWindow->AddMessage(str);
				}

				if (!hitScanState.hasPlayedHeadshotSound && type == HitTypeHead) {
					Handle<IAudioChunk> c =
					  audioDevice->RegisterSound("Sounds/Feedback/HeadshotFeedback.opus");
					AudioParam param;
					param.volume = cg_headshotFeedbackSoundGain;
					audioDevice->PlayLocal(c.GetPointerOrNull(), param);

					hitScanState.hasPlayedHeadshotSound = true;
				}

				hitFeedbackIconState = 1.0F;
				hitFeedbackFriendly = by.IsTeammate(hurtPlayer);
				lastHitTime = world->GetTime();
			}
		}

		void Client::BulletNearPlayer(spades::client::Player& by) {
			SPADES_MARK_FUNCTION();

			// register near shots
			weaponStats.shots[by.GetWeaponType()]++;
		}

		void Client::BulletHitBlock(Vector3 hitPos, IntVector3 blockPos, IntVector3 normal) {
			SPADES_MARK_FUNCTION();

			Vector3 shiftedHitPos = hitPos + (MakeVector3(normal) * 0.1F);

			static constexpr std::array<const char*, 4> snds = {
			  "Sounds/Weapons/Impacts/Ricochet1.opus", "Sounds/Weapons/Impacts/Ricochet2.opus",
			  "Sounds/Weapons/Impacts/Ricochet3.opus", "Sounds/Weapons/Impacts/Ricochet4.opus"
			};
			static constexpr std::array<const char*, 4> wsnds = {
			  "Sounds/Weapons/Impacts/Water1.opus", "Sounds/Weapons/Impacts/Water2.opus",
			  "Sounds/Weapons/Impacts/Water3.opus", "Sounds/Weapons/Impacts/Water4.opus"
			};

			if (blockPos.z >= 63) {
				BulletHitWaterSurface(shiftedHitPos);

				if (!IsMuted()) {
					Handle<IAudioChunk> c = audioDevice->RegisterSound(SampleRandomElement(wsnds));
					AudioParam param;
					param.volume = 2.0F;
					param.pitch = 0.9F + SampleRandomFloat() * 0.2F;
					audioDevice->Play(c.GetPointerOrNull(), shiftedHitPos, param);
				}
			} else {
				uint32_t col = map->GetColor(blockPos.x, blockPos.y, blockPos.z);

				// apply block darkening
				int health = col >> 24;
				uint32_t f = (std::max(health, 32) << 8) / 100;
				col = DarkenColor(col, f);

				col = map->GetColorJit(col); // randomize color
				EmitBlockFragments(shiftedHitPos, IntVectorFromColor(col));

				if (!IsMuted()) {
					Handle<IAudioChunk> c =
					  audioDevice->RegisterSound("Sounds/Weapons/Impacts/Block.opus");
					AudioParam param;
					param.volume = 2.0F;
					audioDevice->Play(c.GetPointerOrNull(), shiftedHitPos, param);

					param.pitch = 0.9F + SampleRandomFloat() * 0.2F;
					c = audioDevice->RegisterSound(SampleRandomElement(snds));
					audioDevice->Play(c.GetPointerOrNull(), shiftedHitPos, param);
				}
			}
		}

		void Client::AddBulletTracer(spades::client::Player& player,
			spades::Vector3 muzzlePos, spades::Vector3 hitPos) {
			SPADES_MARK_FUNCTION();

			int tracersMode = cg_tracers;
			if (tracersMode == 0)
				return;

			// distance cull
			float distSqr = (muzzlePos - lastSceneDef.viewOrigin).GetSquaredLength2D();
			if (distSqr > TRACER_CULL_DIST_SQ)
				return;

			// If disabled, do not display bullet tracers when in firstperson perspective
			bool isFirstPerson = IsInFirstPersonView(player.GetId());
			if (tracersMode == 2 && isFirstPerson)
				return;

			// The line segment containing `muzzlePos` and `hitPos` represents the accurate
			// trajectory of the fired bullet (as far as the game physics is concerned), but
			// displaying it as-is would make it seem like it was fired from a skull gun. Rewrite
			// the starting point with the visual muzzle point of the current weapon skin.
			Handle<ClientPlayer> clientPlayer = clientPlayers[player.GetId()];
			muzzlePos = isFirstPerson ? clientPlayer->GetMuzzlePositionInFirstPersonView()
									  : clientPlayer->GetMuzzlePosition();

			float vel;
			bool shotgun = false;
			switch (player.GetWeapon().GetWeaponType()) {
				case RIFLE_WEAPON: vel = 700.0F; break;
				case SMG_WEAPON: vel = 360.0F; break;
				case SHOTGUN_WEAPON:
					vel = 550.0F;
					shotgun = true;
					break;
				default: vel = 0.0F; break;
			}

			// Not to give the false illusion that the bullets travel slow
			if (isFirstPerson)
				vel *= 2.0F;

			AddLocalEntity(stmp::make_unique<Tracer>(*this, muzzlePos, hitPos, vel, shotgun));
			AddLocalEntity(stmp::make_unique<MapViewTracer>(muzzlePos, hitPos));
		}

		void Client::BlocksFell(std::vector<IntVector3> blocks) {
			SPADES_MARK_FUNCTION();

			if (blocks.empty())
				return;

			Handle<IAudioChunk> c = audioDevice->RegisterSound("Sounds/Misc/BlockBounce.opus");
			AddLocalEntity(stmp::make_unique<FallingBlock>(this, c.GetPointerOrNull(), blocks));

			if (!IsMuted()) {
				Vector3 origin = MakeVector3(blocks[0]) + 0.5F;
				Handle<IAudioChunk> c = audioDevice->RegisterSound("Sounds/Misc/BlockFall.opus");
				audioDevice->Play(c.GetPointerOrNull(), origin, AudioParam());
			}
		}

		void Client::GrenadeBounced(const Grenade& g) {
			SPADES_MARK_FUNCTION();

			Vector3 origin = g.GetPosition();

			if (!IsMuted() && origin.z < 63.0F) {
				Handle<IAudioChunk> c =
				  audioDevice->RegisterSound("Sounds/Weapons/Grenade/Bounce.opus");
				audioDevice->Play(c.GetPointerOrNull(), origin, AudioParam());
			}
		}

		void Client::GrenadeDroppedIntoWater(const Grenade& g) {
			SPADES_MARK_FUNCTION();

			if (!IsMuted()) {
				Handle<IAudioChunk> c =
				  audioDevice->RegisterSound("Sounds/Weapons/Grenade/DropWater.opus");
				audioDevice->Play(c.GetPointerOrNull(), g.GetPosition(), AudioParam());
			}
		}

		void Client::GrenadeExploded(const Grenade& g) {
			SPADES_MARK_FUNCTION();

			Vector3 origin = g.GetPosition();

			if (origin.z >= 63.0F) {
				GrenadeExplosionUnderwater(origin);

				if (!IsMuted()) {
					Handle<IAudioChunk> c =
					  audioDevice->RegisterSound("Sounds/Weapons/Grenade/WaterExplode.opus");
					AudioParam param;
					param.volume = 10.0F;
					audioDevice->Play(c.GetPointerOrNull(), origin, param);

					c = audioDevice->RegisterSound("Sounds/Weapons/Grenade/WaterExplodeFar.opus");
					param.volume = 6.0F;
					param.referenceDistance = 10.0F;
					audioDevice->Play(c.GetPointerOrNull(), origin, param);

					c = audioDevice->RegisterSound("Sounds/Weapons/Grenade/WaterExplodeStereo.opus");
					param.volume = 2.0F;
					audioDevice->Play(c.GetPointerOrNull(), origin, param);
				}
			} else {
				GrenadeExplosion(origin);

				if (!IsMuted()) {
					Handle<IAudioChunk> c, cs;
					switch (SampleRandomInt(0, 1)) {
						case 0:
							c = audioDevice->RegisterSound("Sounds/Weapons/Grenade/Explode1.opus");
							cs = audioDevice->RegisterSound("Sounds/Weapons/Grenade/ExplodeStereo1.opus");
							break;
						case 1:
							c = audioDevice->RegisterSound("Sounds/Weapons/Grenade/Explode2.opus");
							cs = audioDevice->RegisterSound("Sounds/Weapons/Grenade/ExplodeStereo2.opus");
							break;
					}

					AudioParam param;
					param.volume = 30.0F;
					param.referenceDistance = 5.0F;
					audioDevice->Play(c.GetPointerOrNull(), origin, param);

					param.referenceDistance = 1.0F;
					audioDevice->Play(cs.GetPointerOrNull(), origin, param);

					c = audioDevice->RegisterSound("Sounds/Weapons/Grenade/ExplodeFar.opus");
					param.volume = 6.0F;
					param.referenceDistance = 40.0F;
					audioDevice->Play(c.GetPointerOrNull(), origin, param);

					c = audioDevice->RegisterSound("Sounds/Weapons/Grenade/ExplodeFarStereo.opus");
					param.referenceDistance = 10.0F;
					audioDevice->Play(c.GetPointerOrNull(), origin, param);

					// debri sound
					c = audioDevice->RegisterSound("Sounds/Weapons/Grenade/Debris.opus");
					param.volume = 5.0F;
					param.referenceDistance = 3.0F;
					IntVector3 outPos;
					Vector3 soundPos = origin;
					if (world->GetMap()->CastRay(soundPos, MakeVector3(0, 0, 1), 8.0F, outPos))
						soundPos.z = (float)outPos.z - 0.2F;
					audioDevice->Play(c.GetPointerOrNull(), soundPos, param);
				}
			}
		}

		void Client::LocalPlayerBlockAction(spades::IntVector3 v, BlockActionType type) {
			SPADES_MARK_FUNCTION();
			activeNet->SendBlockAction(v, type);
		}
		void Client::LocalPlayerCreatedLineBlock(spades::IntVector3 v1, spades::IntVector3 v2) {
			SPADES_MARK_FUNCTION();
			activeNet->SendBlockLine(v1, v2);
		}

		void Client::LocalPlayerHurt(HurtType type, spades::Vector3 source) {
			SPADES_MARK_FUNCTION();

			if (source.GetSquaredLength() < 0.01F) {
				stmp::optional<Player&> p = world->GetLocalPlayer();
				if (p && p->GetWade())
					ShowAlert(_Tr("Client", "You are taking damage. Find solid ground!"), AlertType::Warning, 1.5F, true);
				return;
			}

			stmp::optional<Player&> p = world->GetLocalPlayer();
			if (!p)
				return;
			Vector3 rel = source - p->GetEye();
			rel.z = 0.0F;
			rel = rel.Normalize();
			hurtRingView->Add(rel);
		}

		void Client::LocalPlayerBuildError(BuildFailureReason reason) {
			SPADES_MARK_FUNCTION();

			switch (reason) {
				case BuildFailureReason::InsufficientBlocks:
					ShowAlert(_Tr("Client", "Insufficient blocks."), AlertType::Error);
					break;
				case BuildFailureReason::InvalidPosition:
					ShowAlert(_Tr("Client", "You cannot place a block there."), AlertType::Error);
					break;
			}
		}
	} // namespace client
} // namespace spades
