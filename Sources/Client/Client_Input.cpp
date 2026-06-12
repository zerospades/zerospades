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

#include <cstdio>

#include <Core/Settings.h>
#include <Core/Strings.h>

#include "IAudioChunk.h"
#include "IAudioDevice.h"

#include "ChatWindow.h"
#include "ClientUI.h"
#include "Corpse.h"
#include "GameProperties.h"
#include "LimboView.h"
#include "MapView.h"
#include "PaletteView.h"
#include "PieMenuView.h"

#include "GameMap.h"
#include "Weapon.h"
#include "World.h"

#include "NetClient.h"

using namespace std;

DEFINE_SPADES_SETTING(cg_mouseSensitivity, "1");
DEFINE_SPADES_SETTING(cg_zoomedMouseSensScale, "1");
DEFINE_SPADES_SETTING(cg_mouseSensScale, "0");
DEFINE_SPADES_SETTING(cg_mouseAccel, "1");
DEFINE_SPADES_SETTING(cg_mouseExpPower, "1");
DEFINE_SPADES_SETTING(cg_invertMouseY, "0");

DEFINE_SPADES_SETTING(cg_holdAimDownSight, "0");
DEFINE_SPADES_SETTING(cg_holdMapZoom, "0");

DEFINE_SPADES_SETTING(cg_keyAttack, "LeftMouseButton");
DEFINE_SPADES_SETTING(cg_keyAltAttack, "RightMouseButton");
DEFINE_SPADES_SETTING(cg_keyToolSpade, "1");
DEFINE_SPADES_SETTING(cg_keyToolBlock, "2");
DEFINE_SPADES_SETTING(cg_keyToolWeapon, "3");
DEFINE_SPADES_SETTING(cg_keyToolGrenade, "4");
DEFINE_SPADES_SETTING(cg_keyReloadWeapon, "r");
DEFINE_SPADES_SETTING(cg_keyFlashlight, "f");
DEFINE_SPADES_SETTING(cg_keyLastTool, "");

DEFINE_SPADES_SETTING(cg_keyMoveLeft, "a");
DEFINE_SPADES_SETTING(cg_keyMoveRight, "d");
DEFINE_SPADES_SETTING(cg_keyMoveForward, "w");
DEFINE_SPADES_SETTING(cg_keyMoveBackward, "s");
DEFINE_SPADES_SETTING(cg_keyJump, "Space");
DEFINE_SPADES_SETTING(cg_keyCrouch, "Control");
DEFINE_SPADES_SETTING(cg_keySprint, "Shift");
DEFINE_SPADES_SETTING(cg_keySneak, "v");

DEFINE_SPADES_SETTING(cg_keyCaptureColor, "e");
DEFINE_SPADES_SETTING(cg_keyGlobalChat, "t");
DEFINE_SPADES_SETTING(cg_keyTeamChat, "y");
DEFINE_SPADES_SETTING(cg_keyChatLog, "j");
DEFINE_SPADES_SETTING(cg_keyZoomChatLog, "h");
DEFINE_SPADES_SETTING(cg_keyChangeMapScale, "m");
DEFINE_SPADES_SETTING(cg_keyToggleMapZoom, "n");
DEFINE_SPADES_SETTING(cg_keyToggleHitTestZoom, "g");
DEFINE_SPADES_SETTING(cg_keyScoreboard, "Tab");
DEFINE_SPADES_SETTING(cg_keyLimbo, "l");

DEFINE_SPADES_SETTING(cg_keyScreenshot, "0");
DEFINE_SPADES_SETTING(cg_keySceneshot, "9");
DEFINE_SPADES_SETTING(cg_keySaveMap, "8");

DEFINE_SPADES_SETTING(cg_switchToolByWheel, "1");
DEFINE_SPADES_SETTING(cg_debugCorpse, "0");
DEFINE_SPADES_SETTING(cg_keySpawnCorpse, "p");

DEFINE_SPADES_SETTING(cg_keyPieMenu, "MiddleMouseButton");

SPADES_SETTING(cg_manualFocus);
DEFINE_SPADES_SETTING(cg_keyAutoFocus, "MiddleMouseButton");

DEFINE_SPADES_SETTING(cg_keyToggleSpectatorNames, "z");
DEFINE_SPADES_SETTING(cg_keySpectatorZoom, "e");
DEFINE_SPADES_SETTING(cg_keyStaffSpectating, "f5");
DEFINE_SPADES_SETTING(cg_keyDemoPlayPause, "p");
DEFINE_SPADES_SETTING(cg_keyDemoSeekForward, "Right");
DEFINE_SPADES_SETTING(cg_keyDemoSeekBackward, "Left");
DEFINE_SPADES_SETTING(cg_keyDemoRecord, "F9");
DEFINE_SPADES_SETTING(cg_keyDemoSpeedUp, ".");
DEFINE_SPADES_SETTING(cg_keyDemoSlowDown, ",");
DEFINE_SPADES_SETTING(cg_keyDemoToggleHud, "b");
SPADES_SETTING(cg_demoMaxFiles);
SPADES_SETTING(cg_demoAutoPrune);

SPADES_SETTING(s_volume);
DEFINE_SPADES_SETTING(cg_keyVolumeUp, "+");
DEFINE_SPADES_SETTING(cg_keyVolumeDown, "-");

SPADES_SETTING(cg_debugHitTest);

DEFINE_SPADES_SETTING(cg_keyToggleHud, "Home");
SPADES_SETTING(cg_hideHud);

DEFINE_SPADES_SETTING(cg_keyToggleLeftHand, "c");
SPADES_SETTING(cg_viewWeaponSide);

namespace spades {
	namespace client {

		bool Client::WantsToBeClosed() { return readyToClose; }
		void Client::Closing() { SPADES_MARK_FUNCTION(); }

		bool Client::NeedsAbsoluteMouseCoordinate() {
			SPADES_MARK_FUNCTION();

			if (scriptedUI->NeedsInput())
				return true;
			if (!world)
				return true; // now loading.
			if (IsLimboViewActive())
				return true;

			return false;
		}

		void Client::MouseEvent(float x, float y) {
			SPADES_MARK_FUNCTION();

			if (scriptedUI->NeedsInput()) {
				scriptedUI->MouseEvent(x, y);
				return;
			}

			if (IsLimboViewActive()) {
				limbo->MouseEvent(x, y);
				return;
			}

			if (pieMenuView && pieMenuView->IsOpen()) {
				pieMenuView->HandleMouseDelta(x, y);
				return;
			}

			auto cameraMode = GetCameraMode();

			switch (cameraMode) {
				case ClientCameraMode::None:
				case ClientCameraMode::NotJoined:
				case ClientCameraMode::FirstPersonFollow:
					// No-op
					break;
				case ClientCameraMode::Free:
				case ClientCameraMode::ThirdPersonLocal:
				case ClientCameraMode::ThirdPersonFollow: {
					// Move the third-person or free-floating camera
					x = -x;
					if (!cg_invertMouseY)
						y = -y;

					// scale by FOV (when zooming)
					if (spectatorZoomState > 0.0F) {
						float k = atanf(tanf(lastSceneDef.fovX * 0.5F));

						x *= k;
						y *= k;
					}

					auto& sharedState = followAndFreeCameraState;
					sharedState.yaw -= x * 0.003F;
					sharedState.pitch -= y * 0.003F;
					if (sharedState.pitch > DEG2RAD(89))
						sharedState.pitch = DEG2RAD(89);
					if (sharedState.pitch < -DEG2RAD(89))
						sharedState.pitch = -DEG2RAD(89);
					sharedState.yaw = fmodf(sharedState.yaw, DEG2RAD(360));
					break;
				}
				case ClientCameraMode::FirstPersonLocal: {
					SPAssert(world);

					stmp::optional<Player&> maybePlayer = world->GetLocalPlayer();
					SPAssert(maybePlayer);

					Player& p = maybePlayer.value();
					if (p.IsAlive()) {
						float sensitivity = cg_mouseSensitivity;

						// scale by FOV (when zooming)
						float aimDownState = GetAimDownState();
						if (aimDownState > 0.0F) {
							float k = atanf(tanf(lastSceneDef.fovX * 0.5F));

							x *= k;
							y *= k;
						}

						// TODO: mouse acceleration is framerate dependant
						if (cg_mouseAccel) {
							float rad = x * x + y * y;
							if (rad > 0.0F) {
								if ((float)cg_mouseExpPower < 0.001F || isnan((float)cg_mouseExpPower)) {
									const auto& defaultValue = cg_mouseExpPower.GetDescriptor().defaultValue;
									SPLog("Invalid cg_mouseExpPower value: \"%s\", resetting to \"%s\"",
										cg_mouseExpPower.CString(), defaultValue.c_str());
									cg_mouseExpPower = defaultValue;
								}

								float const fExp = powf(renderer->ScreenWidth() * 0.1F, 2);
								rad = powf(rad / fExp, (float)cg_mouseExpPower * 0.5F - 0.5F);

								// shouldn't happen...
								if (isnan(rad))
									rad = 1.0F;

								x *= rad;
								y *= rad;
							}
						}

						if (aimDownState > 0.0F) {
							float scale = cg_zoomedMouseSensScale;
							scale = powf(scale, aimDownState);

							x *= scale;
							y *= scale;
						}

						x *= sensitivity;
						y *= sensitivity;

						float sensScale;
						switch ((int)cg_mouseSensScale) {
							case 1: // voxlap
								sensScale = 1.0F / 10.0F * 0.005F;
								break;
							case 2: // betterspades
								sensScale = 1.0F / 5.0F * 0.002F;
								break;
							case 3: // quake/source
								sensScale = DEG2RAD(0.022F);
								break;
							case 4: // overwatch
								sensScale = DEG2RAD(0.0066F);
								break;
							case 5: // valorant
								sensScale = DEG2RAD(0.07F);
								break;
							default: // openspades
								sensScale = 0.003F;
								break;
						}

						x *= sensScale;
						y *= sensScale;

						if (!cg_invertMouseY)
							y = -y;

						p.Turn(x, y);
					}

					break;
				}
			}
		}

		void Client::WheelEvent(float x, float y) {
			SPADES_MARK_FUNCTION();

			if (scriptedUI->NeedsInput()) {
				scriptedUI->WheelEvent(x, y);
				return;
			}

			if (y > 0.5F) {
				KeyEvent("WheelDown", true);
				KeyEvent("WheelDown", false);
			} else if (y < -0.5F) {
				KeyEvent("WheelUp", true);
				KeyEvent("WheelUp", false);
			}
		}

		void Client::TextInputEvent(const std::string& ch) {
			SPADES_MARK_FUNCTION();

			if (scriptedUI->NeedsInput() && !scriptedUI->IsIgnored(ch)) {
				scriptedUI->TextInputEvent(ch);
				return;
			}

			// we don't get "/" here anymore
		}

		void Client::TextEditingEvent(const std::string& ch, int start, int len) {
			SPADES_MARK_FUNCTION();

			if (scriptedUI->NeedsInput() && !scriptedUI->IsIgnored(ch)) {
				scriptedUI->TextEditingEvent(ch, start, len);
				return;
			}
		}

		bool Client::AcceptsTextInput() {
			SPADES_MARK_FUNCTION();
			if (scriptedUI->NeedsInput())
				return scriptedUI->AcceptsTextInput();
			return false;
		}

		AABB2 Client::GetTextInputRect() {
			SPADES_MARK_FUNCTION();
			if (scriptedUI->NeedsInput())
				return scriptedUI->GetTextInputRect();
			return AABB2();
		}

		static bool CheckKey(const std::string& cfg, const std::string& input) {
			if (cfg.empty())
				return false;

			static const std::string space1("space");
			static const std::string space2("spacebar");
			static const std::string space3("spacekey");

			if (EqualsIgnoringCase(cfg, space1) ||
				EqualsIgnoringCase(cfg, space2) ||
				EqualsIgnoringCase(cfg, space3)) {
				if (input == " ")
					return true;
			} else {
				if (EqualsIgnoringCase(cfg, input))
					return true;
			}
			return false;
		}

		void Client::KeyEvent(const std::string& name, bool down) {
			SPADES_MARK_FUNCTION();

			if (scriptedUI->NeedsInput()) {
				if (!scriptedUI->IsIgnored(name)) {
					scriptedUI->KeyEvent(name, down);
				} else {
					if (!down)
						scriptedUI->SetIgnored("");
				}
				return;
			}

			if (name == "Escape") {
				if (down) {
					if (inGameLimbo) {
						inGameLimbo = false;
					} else {
						if (GetWorld() == nullptr) {
							// loading now, abort download, and quit the game immediately.
							readyToClose = true;
						} else {
							scriptedUI->EnterClientMenu();
						}
					}
				}
			} else if (world) {
				stmp::optional<Player&> maybePlayer = world->GetLocalPlayer();

				// volume control
				if ((CheckKey(cg_keyVolumeDown, name) || CheckKey(cg_keyVolumeUp, name)) && down) {
					int volume = s_volume;

					if (CheckKey(cg_keyVolumeDown, name))
						volume = std::max(volume - 10, 0);
					else if (CheckKey(cg_keyVolumeUp, name))
						volume = std::min(volume + 10, 100);

					s_volume = volume;
					ShowAlert(_Tr("Client", "Volume: {0}%", volume), AlertType::Notice);
				}

				if (IsLimboViewActive()) {
					if ((CheckKey(cg_keyLimbo, name) && down) && maybePlayer) {
						inGameLimbo = false;
						return;
					}
					if (down)
						limbo->KeyEvent(name);
					return;
				}

				auto cameraMode = GetCameraMode();

				// In demo mode, we're always spectating without a local player
				bool demoMode = IsDemoMode();
				if (demoMode) {
					// Allow screenshot / mapshot keys during playback — they don't
					// require a local player and are commonly used to capture demos.
					if (CheckKey(cg_keySceneshot, name) && down) {
						TakeScreenShot(true);
						return;
					} else if (CheckKey(cg_keyScreenshot, name) && down) {
						TakeScreenShot(false);
						return;
					} else if (CheckKey(cg_keySaveMap, name) && down) {
						TakeMapShot();
						return;
					} else if (CheckKey(cg_keyStaffSpectating, name) && down) {
						staffSpectating = !staffSpectating;
						Handle<IAudioChunk> c =
						  audioDevice->RegisterSound("Sounds/Player/Flashlight.opus");
						audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
						return;
					} else if (CheckKey(cg_keyDemoPlayPause, name) && down) {
						if (demoNet->IsFinished()) {
							// Demo finished - reload from file to reset world state
							ReloadDemo();
						} else {
							// Toggle pause
							demoNet->TogglePause();
						}
						return;
					}

					// Handle demo seeking (arrow keys).
					//
					// While a seek key is held we only call SeekPreview(), which updates the
					// displayed playback position cheaply (no world reset/replay). RunFrame
					// advances the pending target on each repeat tick.	 The full Seek() —
					// which resets and replays the world — fires once on key release so the
					// expensive operation happens at most once per key press.
					if (CheckKey(cg_keyDemoSeekForward, name)) {
						if (down && !demoSeekForwardHeld) {
							float demoTime = demoNet->GetTime();
							float maxTime = demoNet->GetDuration();
							if (demoTime < maxTime) {
								float target = std::min(maxTime, demoTime + 5.0F);
								demoSeekForwardHeld = true;
								demoSeekRepeatTimer = 0.0F;
								demoSeekPendingTime = target;
								demoNet->SeekPreview(demoSeekPendingTime);
							}
						} else if (!down && demoSeekForwardHeld) {
							demoSeekForwardHeld = false;
							demoNet->Seek(demoSeekPendingTime);
						}
						return;
					} else if (CheckKey(cg_keyDemoSeekBackward, name)) {
						if (down && !demoSeekBackwardHeld) {
							float demoTime = demoNet->GetTime();
							float minTime = demoNet->GetBootstrapEndTime();
							if (demoTime > minTime) {
								float target = std::max(minTime, demoTime - 5.0F);
								demoSeekBackwardHeld = true;
								demoSeekRepeatTimer = 0.0F;
								demoSeekPendingTime = target;
								demoNet->SeekPreview(demoSeekPendingTime);
							 }
						} else if (!down && demoSeekBackwardHeld) {
							demoSeekBackwardHeld = false;
							demoNet->Seek(demoSeekPendingTime);
						}
						return;
					}

					// Speed control: step through preset multipliers
					static const float speedSteps[] = {0.25F, 0.5F, 1.0F, 2.0F, 4.0F};
					static const int numSpeedSteps = 5;
					if (CheckKey(cg_keyDemoSpeedUp, name) && down) {
						float cur = demoNet->GetSpeed();
						for (int i = 0; i < numSpeedSteps - 1; i++) {
							if (cur < speedSteps[i] * 1.1F) {
								demoNet->SetSpeed(speedSteps[i + 1]);
								break;
							}
						}
						return;
					} else if (CheckKey(cg_keyDemoSlowDown, name) && down) {
						float cur = demoNet->GetSpeed();
						for (int i = numSpeedSteps - 1; i > 0; i--) {
							if (cur > speedSteps[i] * 0.9F) {
								demoNet->SetSpeed(speedSteps[i - 1]);
								break;
							}
						}
						return;
					}

					// Handle demo spectator controls
					if (cameraMode == ClientCameraMode::Free) {
						if (CheckKey(cg_keyAttack, name)) {
							if (down) {
								// reset zoom
								if (spectatorZoom) {
									spectatorZoom = false;
									spectatorZoomState = 0.0F;
								}
								// Start following the recorded player
								followedPlayerId = demoNet->GetRecordedLocalPlayerId();
								FollowNextPlayer(false);
							}
							return;
						} else if (CheckKey(cg_keyAltAttack, name)) {
							if (down) {
								// reset zoom
								if (spectatorZoom) {
									spectatorZoom = false;
									spectatorZoomState = 0.0F;
								}
								// Start following the recorded player
								followedPlayerId = demoNet->GetRecordedLocalPlayerId();
								FollowNextPlayer(true);
							}
							return;
						} else if (CheckKey(cg_keyToggleSpectatorNames, name) && down) {
							spectatorPlayerNames = !spectatorPlayerNames;
							Handle<IAudioChunk> c =
							  audioDevice->RegisterSound("Sounds/Player/Flashlight.opus");
							audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
							return;
						} else if (CheckKey(cg_keySpectatorZoom, name)) {
							spectatorZoom = down;
							return;
						}
					} else if (cameraMode == ClientCameraMode::FirstPersonFollow ||
							   cameraMode == ClientCameraMode::ThirdPersonFollow) {
						if (CheckKey(cg_keyAttack, name)) {
							if (down) {
								if (spectatorZoom) {
									spectatorZoom = false;
									spectatorZoomState = 0.0F;
								}
								FollowNextPlayer(false);
							}
							return;
						} else if (CheckKey(cg_keyAltAttack, name)) {
							if (down) {
								if (spectatorZoom) {
									spectatorZoom = false;
									spectatorZoomState = 0.0F;
								}
								FollowNextPlayer(true);
							}
							return;
						} else if (CheckKey(cg_keyJump, name)) {
							auto maybeTarget = world->GetPlayer(GetCameraTargetPlayerId());
							if (down && maybeTarget && maybeTarget->IsAlive())
								followCameraState.firstPerson = !followCameraState.firstPerson;
							return;
						} else if (CheckKey(cg_keyReloadWeapon, name) && followCameraState.enabled) {
							if (down) {
								playerInput.jump = PlayerInput().jump;
								if (spectatorZoom) {
									spectatorZoom = false;
									spectatorZoomState = 0.0F;
								}
								followCameraState.enabled = false;
							}
							return;
						} else if (CheckKey(cg_keyToggleSpectatorNames, name) && down) {
							spectatorPlayerNames = !spectatorPlayerNames;
							Handle<IAudioChunk> c =
							  audioDevice->RegisterSound("Sounds/Player/Flashlight.opus");
							audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
							return;
						}
					}

					// Handle movement for demo spectator free camera
					if (CheckKey(cg_keyMoveLeft, name)) {
						playerInput.moveLeft = down;
						keypadInput.left = down;
						playerInput.moveRight = down ? false : keypadInput.right;
					} else if (CheckKey(cg_keyMoveRight, name)) {
						playerInput.moveRight = down;
						keypadInput.right = down;
						playerInput.moveLeft = down ? false : keypadInput.left;
					} else if (CheckKey(cg_keyMoveForward, name)) {
						playerInput.moveForward = down;
						keypadInput.forward = down;
						playerInput.moveBackward = down ? false : keypadInput.backward;
					} else if (CheckKey(cg_keyMoveBackward, name)) {
						playerInput.moveBackward = down;
						keypadInput.backward = down;
						playerInput.moveForward = down ? false : keypadInput.forward;
					} else if (CheckKey(cg_keyCrouch, name)) {
						playerInput.crouch = down;
					} else if (CheckKey(cg_keySprint, name)) {
						playerInput.sprint = down;
					} else if (CheckKey(cg_keyJump, name)) {
						playerInput.jump = down;
					}

					// Handle map controls in demo mode
					if (CheckKey(cg_keyChangeMapScale, name) && down) {
						if (!largeMapView->IsZoomed()) {
							renderer->UpdateFlatGameMap();
							mapView->SwitchScale();
							Handle<IAudioChunk> c =
							  audioDevice->RegisterSound("Sounds/Misc/SwitchMapZoom.opus");
							audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
						}
					} else if (CheckKey(cg_keyToggleMapZoom, name)) {
						if (down || cg_holdMapZoom) {
							bool zoomed = largeMapView->IsZoomed();
							zoomed = !zoomed;
							if (cg_holdMapZoom)
								zoomed = down;

							renderer->UpdateFlatGameMap();
							largeMapView->SetZoom(zoomed);
							Handle<IAudioChunk> c = zoomed
								? audioDevice->RegisterSound("Sounds/Misc/OpenMap.opus")
								: audioDevice->RegisterSound("Sounds/Misc/CloseMap.opus");
							audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
						}
					} else if (CheckKey(cg_keyScoreboard, name)) {
						scoreboardVisible = down;
					} else if (CheckKey(cg_keyToggleHud, name) && down) {
						if (cg_hideHud) {
							cg_hideHud = 0;
							hudVisible = true;
						} else {
							hudVisible = !hudVisible;
						}

						if (!hudVisible) {
							ShowAlert(_Tr("Client", "Press [{0}] to enable HUD",
								_Tr("Client", ToUpperCase(name))), AlertType::Notice);
						}

						Handle<IAudioChunk> c =
						  audioDevice->RegisterSound("Sounds/Player/Flashlight.opus");
						audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
					} else if (CheckKey(cg_keyChatLog, name) && down) {
						scriptedUI->EnterChatLogWindow();
						scriptedUI->SetIgnored(name);
					} else if (CheckKey(cg_keyZoomChatLog, name)) {
						chatWindow->SetExpanded(down);
					} else if (CheckKey(cg_keyDemoToggleHud, name) && down) {
						demoHudVisible = !demoHudVisible;

						Handle<IAudioChunk> c =
						  audioDevice->RegisterSound("Sounds/Player/Flashlight.opus");
						audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
					}
					return;
				} // end of demo input

				if (!maybePlayer)
					return;

				Player& p = maybePlayer.value();

				bool localPlayerIsAlive = p.IsAlive();
				bool localPlayerIsSpectator = p.IsSpectator();
				bool localPlayerIsSpectating = localPlayerIsSpectator || staffSpectating;
				bool isStaff = activeNet->GetGameProperties()->isStaff;

				// Pie menu: hold to open, release to commit.
				// Aim at a teammate to send a DM; otherwise broadcast on team chat.
				if (CheckKey(cg_keyPieMenu, name) && localPlayerIsAlive && !localPlayerIsSpectating) {
					if (down && !pieMenuView->IsOpen()) {
						auto hot = HotTrackedPlayer();
						if (hot) {
							Player& target = std::get<0>(*hot);
							pieMenuView->Open(PieMenuView::Variant::Player, target.GetId());
						} else {
							pieMenuView->Open(PieMenuView::Variant::World);
						}
						weapInput = WeaponInput();
					} else if (!down && pieMenuView->IsOpen()) {
						PieMenuView::Variant v = pieMenuView->GetVariant();
						int targetId = pieMenuView->GetTargetPlayerId();
						const auto& labels = pieMenuView->GetLabels();
						int sel = pieMenuView->Close();
						if (sel >= 0 && sel < PieMenuView::kSliceCount && net) {
							const std::string& msg = labels[static_cast<size_t>(sel)];
							if (v == PieMenuView::Variant::Player && targetId >= 0) {
								char cmd[128];
								std::snprintf(cmd, sizeof(cmd), "/pm #%d %s", targetId, msg.c_str());
								net->SendChat(cmd, false);
							} else if (v == PieMenuView::Variant::World) {
								net->SendChat(msg, false);
							}
						}
					}
					return;
				}

				// Swallow attack inputs while the pie menu is open
				if (pieMenuView->IsOpen()) {
					if (CheckKey(cg_keyAttack, name) || CheckKey(cg_keyAltAttack, name))
						return;
				}

				switch (cameraMode) {
					case ClientCameraMode::None:
					case ClientCameraMode::NotJoined:
					case ClientCameraMode::FirstPersonLocal: break;
					case ClientCameraMode::ThirdPersonLocal:
						if (localPlayerIsAlive)
							break;
						[[fallthrough]];
					case ClientCameraMode::FirstPersonFollow:
					case ClientCameraMode::ThirdPersonFollow:
					case ClientCameraMode::Free: {
						if (CheckKey(cg_keyAttack, name)) {
							if (down) {
								// reset zoom
								if (spectatorZoom) {
									spectatorZoom = false;
									spectatorZoomState = 0.0F;
								}

								// Start with the local player
								if (cameraMode == ClientCameraMode::Free ||
									cameraMode == ClientCameraMode::ThirdPersonLocal)
									followedPlayerId = p.GetId();
								if (localPlayerIsSpectating || time - lastAliveTime > 1.3F)
									FollowNextPlayer(false);
							}
							return;
						} else if (CheckKey(cg_keyAltAttack, name)) {
							if (down) {
								// reset zoom
								if (spectatorZoom) {
									spectatorZoom = false;
									spectatorZoomState = 0.0F;
								}

								// Start with the local player
								if (cameraMode == ClientCameraMode::Free ||
									cameraMode == ClientCameraMode::ThirdPersonLocal)
									followedPlayerId = p.GetId();
								if (localPlayerIsSpectating || time - lastAliveTime > 1.3F)
									FollowNextPlayer(true);
							}
							return;
						} else if (CheckKey(cg_keyJump, name) && cameraMode != ClientCameraMode::Free) {
							auto maybeTarget = world->GetPlayer(GetCameraTargetPlayerId());
							if (down && maybeTarget && maybeTarget->IsAlive())
								followCameraState.firstPerson = !followCameraState.firstPerson;
							return;
						} else if (CheckKey(cg_keyReloadWeapon, name)
							&& localPlayerIsSpectating && followCameraState.enabled) {
							if (down) {
								// reset jump
								playerInput.jump = PlayerInput().jump;

								// reset zoom
								if (spectatorZoom) {
									spectatorZoom = false;
									spectatorZoomState = 0.0F;
								}

								// Unfollow
								followCameraState.enabled = false;
							}
							return;
						}
						break;
					}
				}

				// demo record toggle accessible for both players and spectators
				if (CheckKey(cg_keyDemoRecord, name) && down && !demoMode) {
					if (net->IsDemoRecording()) {
						net->StopDemoRecording();
						ShowAlert(_Tr("Client", "Recording stopped"), AlertType::Notice);
					} else {
						if (net->StartDemoRecording("", BuildDemoContext())) {
							if ((int)cg_demoAutoPrune != 0) {
								int maxDemos = (int)cg_demoMaxFiles;
								if (maxDemos >= 1)
									DemoRecorder::PruneOldRecordings(static_cast<size_t>(maxDemos));
							}

							const auto demoPath = net->GetDemoFilename().c_str();
							SPLog("Demo recording started: %s", demoPath);
							ShowAlert(_Tr("Client", "Recording demo: {0}", demoPath), AlertType::Notice);
						} else {
							ShowAlert(_Tr("Client", "Failed to start demo recording"), AlertType::Error);
						}
					}
					return;
				}

				// player is not spectating
				if (!localPlayerIsSpectating) {
					// hit debugger zoom can be toggled when dead
					if (CheckKey(cg_keyToggleHitTestZoom, name) &&
						debugHitTestImage && cg_debugHitTest) {
						debugHitTestZoom = down;
						Handle<IAudioChunk> c = debugHitTestZoom
							? audioDevice->RegisterSound("Sounds/Misc/OpenMap.opus")
							: audioDevice->RegisterSound("Sounds/Misc/CloseMap.opus");
						audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
					}

					// player is alive and not spectating
					if (localPlayerIsAlive) {
						if (p.IsToolBlock()) {
							if (paletteView->KeyInput(name, down))
								return;
						}

						if (CheckKey(cg_keySpawnCorpse, name) && down && cg_debugCorpse) {
							auto corp = stmp::make_unique<Corpse>(*renderer, *map, p);
							corp->AddImpulse(p.GetFront() * 32.0F);
							corpses.emplace_back(std::move(corp));
							RemoveCorpses();
						}
					}
				} else {
					if (CheckKey(cg_keyToggleSpectatorNames, name) && down) {
						spectatorPlayerNames = !spectatorPlayerNames;
						Handle<IAudioChunk> c =
						  audioDevice->RegisterSound("Sounds/Player/Flashlight.opus");
						audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
					} else if (CheckKey(cg_keySpectatorZoom, name) &&
							   cameraMode == ClientCameraMode::Free) {
						spectatorZoom = down;
					}
				}

				if (CheckKey(cg_keyMoveLeft, name)) {
					playerInput.moveLeft = down;
					keypadInput.left = down;
					playerInput.moveRight = down ? false : keypadInput.right;
				} else if (CheckKey(cg_keyMoveRight, name)) {
					playerInput.moveRight = down;
					keypadInput.right = down;
					playerInput.moveLeft = down ? false : keypadInput.left;
				} else if (CheckKey(cg_keyMoveForward, name)) {
					playerInput.moveForward = down;
					keypadInput.forward = down;
					playerInput.moveBackward = down ? false : keypadInput.backward;
				} else if (CheckKey(cg_keyMoveBackward, name)) {
					playerInput.moveBackward = down;
					keypadInput.backward = down;
					playerInput.moveForward = down ? false : keypadInput.forward;
				} else if (CheckKey(cg_keyCrouch, name)) {
					playerInput.crouch = down;
				} else if (CheckKey(cg_keySprint, name)) {
					playerInput.sprint = down;
				} else if (CheckKey(cg_keySneak, name)) {
					playerInput.sneak = down;
				} else if (CheckKey(cg_keyJump, name)) {
					playerInput.jump = down;
				} else if (CheckKey(cg_keyAttack, name)) {
					weapInput.primary = down;

					if (p.IsToolWeapon() && weapInput.primary && !CanLocalPlayerUseWeapon())
						PlayerDryFiredWeapon(p);
				} else if (CheckKey(cg_keyAltAttack, name)) {
					bool lastVal = weapInput.secondary;
					if (p.IsToolWeapon() && !cg_holdAimDownSight) {
						Weapon& w = p.GetWeapon();
						if (down && (!w.IsReloading() || w.IsReloadSlow()))
							weapInput.secondary = !weapInput.secondary;
					} else {
						weapInput.secondary = down;
					}

					if (p.IsToolWeapon() && weapInput.secondary
						&& !lastVal && CanLocalPlayerUseWeapon()) {
						AudioParam param;
						param.volume = 0.08F;
						Handle<IAudioChunk> c =
						  audioDevice->RegisterSound("Sounds/Weapons/AimDownSightLocal.opus");
						audioDevice->PlayLocal(c.GetPointerOrNull(),
							MakeVector3(0.4F, -0.3F, 0.5F), param);
					}
				} else if (CheckKey(cg_keyReloadWeapon, name)) {
					reloadKeyPressed = down;
				} else if (CheckKey(cg_keyToolSpade, name) && down) {
					if (!localPlayerIsSpectator && localPlayerIsAlive)
						SetSelectedTool(Player::ToolSpade);
				} else if (CheckKey(cg_keyToolBlock, name) && down) {
					if (!localPlayerIsSpectator && localPlayerIsAlive) {
						if (p.IsToolSelectable(Player::ToolBlock))
							SetSelectedTool(Player::ToolBlock);
						else
							ShowAlert(_Tr("Client", "Out of Blocks"), AlertType::Error);
					}
				} else if (CheckKey(cg_keyToolWeapon, name) && down) {
					if (!localPlayerIsSpectator && localPlayerIsAlive) {
						if (p.IsToolSelectable(Player::ToolWeapon))
							SetSelectedTool(Player::ToolWeapon);
						else
							ShowAlert(_Tr("Client", "Out of Ammo"), AlertType::Error);
					}
				} else if (CheckKey(cg_keyToolGrenade, name) && down) {
					if (!localPlayerIsSpectator && localPlayerIsAlive) {
						if (p.IsToolSelectable(Player::ToolGrenade))
							SetSelectedTool(Player::ToolGrenade);
						else
							ShowAlert(_Tr("Client", "Out of Grenades"), AlertType::Error);
					}
				} else if (CheckKey(cg_keyLastTool, name) && down) {
					if (!localPlayerIsSpectator && localPlayerIsAlive) {
						if (hasLastTool && p.IsToolSelectable(lastTool)) {
							hasLastTool = false;
							SetSelectedTool(lastTool);
						}
					}
				} else if (CheckKey(cg_keyGlobalChat, name) && down) {
					scriptedUI->EnterGlobalChatWindow();
					scriptedUI->SetIgnored(name);
				} else if (CheckKey(cg_keyTeamChat, name) && down) {
					scriptedUI->EnterTeamChatWindow();
					scriptedUI->SetIgnored(name);
				} else if (CheckKey(cg_keyChatLog, name) && down) {
					scriptedUI->EnterChatLogWindow();
					scriptedUI->SetIgnored(name);
				} else if (CheckKey(cg_keyZoomChatLog, name)) {
					chatWindow->SetExpanded(down);
				} else if (CheckKey(cg_keyCaptureColor, name) && down) {
					if (!localPlayerIsSpectator && localPlayerIsAlive && p.IsToolBlock())
						CaptureColor();
				} else if (CheckKey(cg_keyChangeMapScale, name) && down) {
					if (!largeMapView->IsZoomed()) {
						renderer->UpdateFlatGameMap();
						mapView->SwitchScale();
						Handle<IAudioChunk> c =
						  audioDevice->RegisterSound("Sounds/Misc/SwitchMapZoom.opus");
						audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
					}
				} else if (CheckKey(cg_keyToggleMapZoom, name)) {
					if (down || cg_holdMapZoom) {
						bool zoomed = largeMapView->IsZoomed();
						zoomed = !zoomed;
						if (cg_holdMapZoom)
							zoomed = down;

						renderer->UpdateFlatGameMap();
						largeMapView->SetZoom(zoomed);
						Handle<IAudioChunk> c = zoomed
							? audioDevice->RegisterSound("Sounds/Misc/OpenMap.opus")
							: audioDevice->RegisterSound("Sounds/Misc/CloseMap.opus");
						audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
					}
				} else if (CheckKey(cg_keyScoreboard, name)) {
					scoreboardVisible = down;
				} else if (CheckKey(cg_keyToggleHud, name) && down) {
					if (cg_hideHud) {
						cg_hideHud = 0;
						hudVisible = true;
					} else {
						hudVisible = !hudVisible;
					}

					if (!hudVisible) {
						ShowAlert(_Tr("Client", "Press [{0}] to enable HUD",
							_Tr("Client", ToUpperCase(name))), AlertType::Notice);
					}

					Handle<IAudioChunk> c =
					  audioDevice->RegisterSound("Sounds/Player/Flashlight.opus");
					audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
				} else if (CheckKey(cg_keyToggleLeftHand, name) && down) {
					if (IsFirstPerson(cameraMode) && GetAimDownState() < 1.0F) {
						cg_viewWeaponSide = (cg_viewWeaponSide == 1) ? -1 : 1;

						// TODO: use a better sound
						Handle<IAudioChunk> c =
						  audioDevice->RegisterSound("Sounds/Misc/OpenMap.opus");
						audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
					}
				} else if (CheckKey(cg_keyStaffSpectating, name) && down && isStaff) {
					staffSpectating = !staffSpectating;

					// enter follow camera state
					if (staffSpectating && !localPlayerIsSpectator) {
						weapInput = WeaponInput();
						playerInput = PlayerInput();

						// reset zoom
						if (spectatorZoom) {
							spectatorZoom = false;
							spectatorZoomState = 0.0F;
						}

						// fallback to local player if the last player being followed doesn't exist
						if (!world->GetPlayer(followedPlayerId))
							followedPlayerId = p.GetId();

						followCameraState.firstPerson = true; // force firstperson
						followCameraState.enabled = true;
					}

					ShowAlert(_Tr("Client", "zerovl {0}",
						staffSpectating ? "ON" : "OFF"), AlertType::Notice);

					// play sound
					Handle<IAudioChunk> c =
					  audioDevice->RegisterSound("Sounds/Player/Flashlight.opus");
					audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
				} else if (CheckKey(cg_keyLimbo, name) && down) {
					inGameLimbo = true;
				} else if (CheckKey(cg_keySceneshot, name) && down) {
					TakeScreenShot(true);
				} else if (CheckKey(cg_keyScreenshot, name) && down) {
					TakeScreenShot(false);
				} else if (CheckKey(cg_keySaveMap, name) && down) {
					TakeMapShot();
				} else if (CheckKey(cg_keyFlashlight, name) && down) {
					// spectators and dead players shouldn't be able to toggle the flashlight
					if (!localPlayerIsSpectator && localPlayerIsAlive) {
						flashlightOn = !flashlightOn;
						flashlightOnTime = time;
						Handle<IAudioChunk> c =
						  audioDevice->RegisterSound("Sounds/Player/Flashlight.opus");
						audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
					}
				} else if (CheckKey(cg_keyAutoFocus, name) && down && cg_manualFocus) {
					autoFocusEnabled = true;
				} else if ((name == "WheelUp" || name == "WheelDown") && down) {
					// When DoF control is enabled,
					// tool switch is overrided by focal length control.
					bool rev = (int)cg_switchToolByWheel > 0;
					if (name == (rev ? "WheelDown" : "WheelUp")) {
						if (cg_manualFocus) {
							float dist = 1.0F / targetFocalLength;
							dist = std::min(dist + 0.01F, 1.0F);
							targetFocalLength = 1.0F / dist;
							autoFocusEnabled = false;
						} else if (!localPlayerIsSpectator && localPlayerIsAlive &&
								   cg_switchToolByWheel) {
							Player::ToolType t = p.GetTool();
							do {
								switch (t) {
									case Player::ToolSpade: t = Player::ToolGrenade; break;
									case Player::ToolBlock: t = Player::ToolSpade; break;
									case Player::ToolWeapon: t = Player::ToolBlock; break;
									case Player::ToolGrenade: t = Player::ToolWeapon; break;
								}
							} while (!p.IsToolSelectable(t));
							SetSelectedTool(t);
						}
					} else if (name == (rev ? "WheelUp" : "WheelDown")) {
						if (cg_manualFocus) {
							float dist = 1.0F / targetFocalLength;
							// limit to fog max distance
							dist = std::max(dist - 0.01F, 1.0F / 128.0F);
							targetFocalLength = 1.0F / dist;
							autoFocusEnabled = false;
						} else if (!localPlayerIsSpectator && localPlayerIsAlive &&
								   cg_switchToolByWheel) {
							Player::ToolType t = p.GetTool();
							do {
								switch (t) {
									case Player::ToolSpade: t = Player::ToolBlock; break;
									case Player::ToolBlock: t = Player::ToolWeapon; break;
									case Player::ToolWeapon: t = Player::ToolGrenade; break;
									case Player::ToolGrenade: t = Player::ToolSpade; break;
								}
							} while (!p.IsToolSelectable(t));
							SetSelectedTool(t);
						}
					}
				}
			}
		}
	} // namespace client
} // namespace spades
