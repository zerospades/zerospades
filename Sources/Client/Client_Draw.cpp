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
#include <cstdlib>

#include "Client.h"

#include <Core/Bitmap.h>
#include <Core/ConcurrentDispatch.h>
#include <Core/FileManager.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Draw/SW/SWRenderer.h>

#include "CTFGameMode.h"
#include "CenterMessageView.h"
#include "ChatWindow.h"
#include "ClientPlayer.h"
#include "ClientUI.h"
#include "Fonts.h"
#include "GameProperties.h"
#include "HitTestDebugger.h"
#include "HurtRingView.h"
#include "IFont.h"
#include "IGameMode.h"
#include "LimboView.h"
#include "MapView.h"
#include "PaletteView.h"
#include "PieMenuView.h"
#include "ScoreboardView.h"
#include "TCProgressView.h"

#include "GameMap.h"
#include "Weapon.h"
#include "World.h"

#include "NetClient.h"

DEFINE_SPADES_SETTING(cg_hitIndicator, "1");
DEFINE_SPADES_SETTING(cg_debugAim, "0");
SPADES_SETTING(cg_keyReloadWeapon);
SPADES_SETTING(cg_keyJump);
SPADES_SETTING(cg_keyAttack);
SPADES_SETTING(cg_keyAltAttack);
SPADES_SETTING(cg_keyCrouch);
SPADES_SETTING(cg_keyLimbo);
SPADES_SETTING(cg_keyToggleSpectatorNames);
SPADES_SETTING(cg_keyStaffSpectating);
SPADES_SETTING(cg_keyDemoPlayPause);
SPADES_SETTING(cg_keyDemoSeekForward);
SPADES_SETTING(cg_keyDemoSeekBackward);
SPADES_SETTING(cg_keyDemoRecord);
SPADES_SETTING(cg_keyDemoSpeedUp);
SPADES_SETTING(cg_keyDemoSlowDown);
DEFINE_SPADES_SETTING(cg_screenshotFormat, "jpeg");
DEFINE_SPADES_SETTING(cg_stats, "0");
DEFINE_SPADES_SETTING(cg_statsSmallFont, "0");
DEFINE_SPADES_SETTING(cg_playerStats, "0");
DEFINE_SPADES_SETTING(cg_playerStatsShowPlacedBlocks, "0");
DEFINE_SPADES_SETTING(cg_playerStatsHeight, "70");
DEFINE_SPADES_SETTING(cg_hideHud, "0");
DEFINE_SPADES_SETTING(cg_hudColor, "0");
DEFINE_SPADES_SETTING(cg_hudColorR, "255");
DEFINE_SPADES_SETTING(cg_hudColorG, "255");
DEFINE_SPADES_SETTING(cg_hudColorB, "255");
DEFINE_SPADES_SETTING(cg_hudAmmoStyle, "0");
DEFINE_SPADES_SETTING(cg_hudSafezoneX, "1");
DEFINE_SPADES_SETTING(cg_hudSafezoneY, "1");
DEFINE_SPADES_SETTING(cg_hudPlayerCount, "0");
DEFINE_SPADES_SETTING(cg_hudHealthBar, "1");
DEFINE_SPADES_SETTING(cg_hudHealthAnimation, "1");
DEFINE_SPADES_SETTING(cg_playerNames, "2");
DEFINE_SPADES_SETTING(cg_playerNameX, "0");
DEFINE_SPADES_SETTING(cg_playerNameY, "0");
DEFINE_SPADES_SETTING(cg_playerNamesDead, "1");
DEFINE_SPADES_SETTING(cg_dbgHitTestSize, "128");
DEFINE_SPADES_SETTING(cg_dbgHitTestFadeTime, "10");
DEFINE_SPADES_SETTING(cg_damageIndicators, "1");
DEFINE_SPADES_SETTING(cg_hurtScreenEffects, "1");
DEFINE_SPADES_SETTING(cg_healScreenEffects, "1");

DEFINE_SPADES_SETTING(cg_hudHotbar, "1");
SPADES_SETTING(cg_keyToolSpade);
SPADES_SETTING(cg_keyToolBlock);
SPADES_SETTING(cg_keyToolWeapon);
SPADES_SETTING(cg_keyToolGrenade);

SPADES_SETTING(cg_hudPalette);
SPADES_SETTING(cg_keyCaptureColor);
SPADES_SETTING(cg_keyPaletteLeft);
SPADES_SETTING(cg_keyPaletteRight);
SPADES_SETTING(cg_keyPaletteUp);
SPADES_SETTING(cg_keyPaletteDown);
SPADES_SETTING(cg_keyExtendedPalette);

SPADES_SETTING(cg_smallFont);
SPADES_SETTING(cg_minimapSize);

namespace spades {
	namespace client {

		enum class ScreenshotFormat { JPG, TGA, PNG };

		namespace {
			ScreenshotFormat GetScreenshotFormat(const std::string& format) {
				if (EqualsIgnoringCase(format, "jpeg")) {
					return ScreenshotFormat::JPG;
				} else if (EqualsIgnoringCase(format, "tga")) {
					return ScreenshotFormat::TGA;
				} else if (EqualsIgnoringCase(format, "png")) {
					return ScreenshotFormat::PNG;
				} else {
					const auto& defaultValue = cg_screenshotFormat.GetDescriptor().defaultValue;
					SPLog("Invalid screenshot format: \"%s\", resetting to \"%s\"",
						format.c_str(), defaultValue.c_str());
					cg_screenshotFormat = defaultValue;
					return GetScreenshotFormat(defaultValue);
				}
			}

			std::string TrKey(const std::string& name) {
				if (name.empty()) {
					return _Tr("Client", "Unbound");
				} else if (name == "LeftMouseButton") {
					return "LMB";
				} else if (name == "RightMouseButton") {
					return "RMB";
				} else {
					return _Tr("Client", ToUpperCase(name));
				}
			}
		} // namespace

		void Client::TakeScreenShot(bool sceneOnly, bool scoreboardOnly) {
			SceneDefinition sceneDef = CreateSceneDefinition();
			lastSceneDef = sceneDef;
			UpdateMatrices();

			// render scene
			flashDlights = flashDlightsOld;
			DrawScene();

			if (scoreboardOnly) {
				scoreboard->Draw();
			} else if (!sceneOnly) {
				// draw 2d
				Draw2D();
			}

			// Well done!
			renderer->FrameDone();

			Handle<Bitmap> bmp = renderer->ReadBitmap();

			try {
				auto name = ScreenShotPath();
				bmp->Save(name);

				std::string msg = sceneOnly
					? _Tr("Client", "Sceneshot saved: {0}", name)
					: _Tr("Client", "Screenshot saved: {0}", name);
				ShowAlert(msg, AlertType::Notice);

				PlayScreenshotSound();
			} catch (const Exception& ex) {
				auto msg = _Tr("Client", "Screenshot failed: ");
				msg += ex.GetShortMessage();
				ShowAlert(msg, AlertType::Error);
				SPLog("Screenshot failed: %s", ex.what());
			} catch (const std::exception& ex) {
				auto msg = _Tr("Client", "Screenshot failed: ");
				msg += ex.what();
				ShowAlert(msg, AlertType::Error);
				SPLog("Screenshot failed: %s", ex.what());
			}
		}

		std::string Client::ScreenShotPath() {
			char bufJpg[32], bufTga[32], bufPng[32];
			const int maxShotIndex = 10000;
			for (int i = 0; i < maxShotIndex; i++) {
				snprintf(bufJpg, sizeof(bufJpg), "Screenshots/shot%04d.jpg", nextScreenShotIndex);
				snprintf(bufTga, sizeof(bufTga), "Screenshots/shot%04d.tga", nextScreenShotIndex);
				snprintf(bufPng, sizeof(bufPng), "Screenshots/shot%04d.png", nextScreenShotIndex);
				if (FileManager::FileExists(bufJpg) ||
					FileManager::FileExists(bufTga) ||
					FileManager::FileExists(bufPng)) {
					nextScreenShotIndex++;
					if (nextScreenShotIndex >= maxShotIndex)
						nextScreenShotIndex = 0;
					continue;
				}

				switch (GetScreenshotFormat(cg_screenshotFormat)) {
					case ScreenshotFormat::JPG: return bufJpg;
					case ScreenshotFormat::TGA: return bufTga;
					case ScreenshotFormat::PNG: return bufPng;
				}
				SPAssert(false);
			}

			SPRaise("No free file name");
		}

#pragma mark - HUD Drawings

		void Client::DrawSplash() {
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 1));
			renderer->DrawImage(nullptr, AABB2(0, 0, sw, sh));

			Handle<IImage> img = renderer->RegisterImage("Gfx/Title/Logo.png");

			Vector2 size = {img->GetWidth(), img->GetHeight()};
			size *= std::min(1.0F, sw / size.x);
			size *= std::min(1.0F, sh / size.y);

			float pulse = fabsf(sinf(time));
			size *= 1.0F - (pulse * (pulse * 0.25F));

			Vector2 pos = (MakeVector2(sw, sh) - size) * 0.5F;

			renderer->SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1));
			renderer->DrawImage(img, AABB2(pos.x, pos.y, size.x, size.y));
		}

		void Client::DrawStartupScreen() {
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			DrawSplash();

			IFont& font = fontManager->GetGuiFont();
			std::string str = _Tr("Client", "NOW LOADING");
			Vector2 size = font.Measure(str);
			Vector2 pos = (MakeVector2(sw, sh) - 16.0F) - size;
			font.DrawShadow(str, pos, 1.0F, MakeVector4(1, 1, 1, 1), MakeVector4(0, 0, 0, 0.5));
		}

		void Client::DrawDemoPlaybackHUD() {
			if (!IsDemoMode() || !demoNet)
				return;

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			float duration = demoNet->GetDuration();
			float cur = demoNet->GetTime();
			float progress = (duration > 0.0f) ? (cur / duration) : 0.0f;

			IFont& font = fontManager->GetGuiFont();

			// --- progress bar geometry ---
			// Reserve a strip below the bar so it sits above the bottom-anchored
			// FPS/stats line (DrawStats with cg_stats == 1) instead of overlapping it.
			// When cg_hudPlayerCount == 2 the alive-count team bar also anchors at the
			// bottom-center, so lift the demo HUD above it as well.
			const float barH = 4.0F;
			const float margin = 8.0F;
			float bottomReserve = font.Measure("X").y + 4.0F;
			if ((int)cg_hudPlayerCount == 2)
				bottomReserve += 48.0F; // clears the 40px team bar plus padding
			const float barY = sh - margin - barH - bottomReserve;
			const float barW = sw - 2.0F * margin;

			// background
			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.5F));
			renderer->DrawFilledRect(margin, barY, margin + barW, barY + barH);

			// filled portion
			bool isPaused = demoNet->IsPaused();
			Vector4 barColor = isPaused
				? MakeVector4(0.7F, 0.7F, 0.7F, 1)
				: MakeVector4(0.2F, 0.8F, 1.0F, 1);
			renderer->SetColorAlphaPremultiplied(barColor);
			renderer->DrawFilledRect(margin, barY, margin + barW * progress, barY + barH);

			// --- time labels ---
			auto fmtTime = [](float t) -> std::string {
				int mins = static_cast<int>(t) / 60;
				int secs = static_cast<int>(t) % 60;
				char buf[16];
				snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
				return buf;
			};

			std::string curStr = fmtTime(cur);
			std::string durStr = fmtTime(duration);

			Vector4 white = MakeVector4(1, 1, 1, 1);
			Vector4 shadow = MakeVector4(0, 0, 0, 0.5F);

			float textY = barY - font.Measure(curStr).y - 4.0F;

			// current time (left)
			font.DrawShadow(curStr, MakeVector2(margin, textY), 1.0F, white, shadow);

			// duration (right)
			Vector2 durSize = font.Measure(durStr);
			font.DrawShadow(durStr, MakeVector2(margin + barW - durSize.x, textY), 1.0F, white, shadow);

			// speed indicator (center)
			char speedBuf[16];
			float spd = demoNet->GetSpeed();
			if (spd == 1.0f)
				snprintf(speedBuf, sizeof(speedBuf), "%.0fx", spd);
			else
				snprintf(speedBuf, sizeof(speedBuf), "%.2gx", spd);
			std::string speedStr = isPaused
				? std::string("|| ") + speedBuf
				: std::string("> ") + speedBuf;
			Vector2 speedSize = font.Measure(speedStr);
			font.DrawShadow(speedStr, MakeVector2((sw - speedSize.x) * 0.5F, textY), 1.0F,
				isPaused ? MakeVector4(0.8F, 0.8F, 0.8F, 1) : white, shadow);
		}

		void Client::DrawRecordingIndicator() {
			if (!net || !net->IsDemoRecording())
				return;

			float sw = renderer->ScreenWidth();

			float recTime = net->GetDemoRecordingTime();
			int mins = static_cast<int>(recTime) / 60;
			int secs = static_cast<int>(recTime) % 60;
			char timeBuf[16];
			snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", mins, secs);

			IFont& font = fontManager->GetGuiFont();

			// Blinking red dot: visible 0.6 s, hidden 0.4 s
			bool dotVisible = fmodf(time, 1.0F) < 0.6F;

			std::string label = std::string(dotVisible ? "\xe2\x97\x8f " : "  ") + "REC " + timeBuf;

			Vector2 size = font.Measure(label);

			// Place top-right, just below the minimap, to avoid overlapping it or
			// the centered playing-time clock. Mirror the minimap's y offset for
			// the stats bar, then add the minimap height and a gap.
			const float margin = 8.0F;
			float minimapY = margin;
			const int statsMode = cg_stats;
			if (statsMode == 2 || (statsMode >= 3 && scoreboardVisible))
				minimapY += cg_statsSmallFont ? 10.0F : 20.0F;
			float minimapSize = Clamp((float)cg_minimapSize, 32.0F, 256.0F);
			// Align with the minimap top, placed to its left
			float y = minimapY;
			float x = sw - margin - minimapSize - margin - size.x;

			Vector4 red = MakeVector4(1, 0.15F, 0.15F, 1);
			Vector4 shadow = MakeVector4(0, 0, 0, 0.5F);
			font.DrawShadow(label, MakeVector2(x, y), 1.0F, red, shadow);
		}

		void Client::DrawPlayingTime() {
			float sw = renderer->ScreenWidth();

			float y = 8.0F;

			const int statsMode = cg_stats;
			if (statsMode == 2 || (statsMode >= 3 && scoreboardVisible))
				y += cg_statsSmallFont ? 10.0F : 20.0F;

			int now = (int)time;
			int hrs = now / 3600;
			int mins = (now % 3600) / 60;
			int secs = now % 60;

			char buf[16];
			if (hrs > 0)
				snprintf(buf, sizeof(buf), "%d:%02d:%02d", hrs, mins, secs);
			else
				snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);

			IFont& font = fontManager->GetHeadingFont();
			Vector2 size = font.Measure(buf);
			Vector2 pos = MakeVector2((sw - size.x) * 0.5F, y);
			font.Draw(buf, pos + MakeVector2(1, 1), 1.0F, MakeVector4(0, 0, 0, 0.5));
			font.Draw(buf, pos, 1.0F, MakeVector4(1, 1, 1, 1));
		}

		void Client::DrawAlivePlayersCount() {
			const int playerCountMode = cg_hudPlayerCount;
			if (playerCountMode >= 3)
				return; // draw on scoreboard

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			float teamBarW = 30.0F;
			float teamBarH = 40.0F;

			float x = sw * 0.5F;
			float y = 8.0F;

			if (playerCountMode < 2 && scoreboardVisible)
				y += 30.0F;

			const int statsMode = cg_stats;
			if ((playerCountMode >= 2 && statsMode == 1) ||
				(playerCountMode < 2 && (statsMode == 2 || (statsMode >= 3 && scoreboardVisible))))
				y += cg_statsSmallFont ? 10.0F : 20.0F;

			float teamBarY = (playerCountMode < 2) ? y : ((sh - y) - teamBarH);

			Handle<IImage> img;
			IFont& font = fontManager->GetHeadingFont();
			Vector2 pos, size;
			std::string str;

			Vector4 white = MakeVector4(1, 1, 1, 1);
			Vector4 col1 = ConvertColorRGBA(world->GetTeamColor(0));
			Vector4 col2 = ConvertColorRGBA(world->GetTeamColor(1));
			Vector4 brightCol1 = col1 + (white - col1) * 0.5F;
			Vector4 brightCol2 = col2 + (white - col2) * 0.5F;
			Vector4 shadowColor = MakeVector4(0, 0, 0, 0.5);

			// draw team bar
			img = renderer->RegisterImage("Gfx/White.tga");
			size.y = 2.0F;
			renderer->SetColorAlphaPremultiplied(brightCol1);
			renderer->DrawImage(img, AABB2(x - teamBarW, teamBarY, teamBarW, size.y));
			renderer->SetColorAlphaPremultiplied(brightCol2);
			renderer->DrawImage(img, AABB2(x, teamBarY, teamBarW, size.y));

			// draw shadow
			size.x = teamBarW * 2;
			size.y = 12.0F;
			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.4F));
			renderer->DrawImage(img,
				AABB2(x - teamBarW, teamBarY + size.y, size.x, -size.y + 2.0F));
			img = renderer->RegisterImage("Gfx/Scoreboard/TopShadow.tga");
			size.y = 32.0F;
			renderer->DrawImage(img,
				AABB2(x - teamBarW, teamBarY + size.y + 12.0F, size.x, -size.y));

			// draw player icon
			img = renderer->RegisterImage("Gfx/User.png");
			float iconSize = 12.0F;
			float iconSpacing = 5.0F;
			pos.x = x - (teamBarW * 0.5F) - (iconSize * 0.5F);
			pos.y = (teamBarY - 2.0F) + iconSize - iconSpacing;
			renderer->SetColorAlphaPremultiplied(shadowColor);
			renderer->DrawImage(img, pos + MakeVector2(1, 1));
			renderer->SetColorAlphaPremultiplied(brightCol1);
			renderer->DrawImage(img, pos);

			pos.x = x + (teamBarW * 0.5F) - (iconSize * 0.5F);
			renderer->SetColorAlphaPremultiplied(shadowColor);
			renderer->DrawImage(img, pos + MakeVector2(1, 1));
			renderer->SetColorAlphaPremultiplied(brightCol2);
			renderer->DrawImage(img, pos);

			// count alive players
			int numAliveTeam1 = 0, numAliveTeam2 = 0;
			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue;
				Player& player = maybePlayer.value();
				if (!player.IsAlive())
					continue;
				int teamId = player.GetTeamId();
				if (teamId == 0)
					numAliveTeam1++;
				else if (teamId == 1)
					numAliveTeam2++;
			}

			// draw player count
			str = ToString(numAliveTeam1);
			size = font.Measure(str);
			pos.x = x - (teamBarW * 0.5F) - (size.x * 0.5F);
			pos.y = (teamBarY - 2.0F) + iconSize + iconSpacing;
			font.Draw(str, pos + MakeVector2(1, 1), 1.0F, shadowColor);
			font.Draw(str, pos, 1.0F, brightCol1);

			str = ToString(numAliveTeam2);
			size = font.Measure(str);
			pos.x = x + (teamBarW * 0.5F) - (size.x * 0.5F);
			font.Draw(str, pos + MakeVector2(1, 1), 1.0F, shadowColor);
			font.Draw(str, pos, 1.0F, brightCol2);
		}

		void Client::DrawHurtSprites() {
			float per = (time - lastHurtTime) / 1.5F;
			if (per < 0.0F || per > 1.0F)
				return;

			Handle<IImage> img = renderer->RegisterImage("Gfx/HurtSprite.png");
			Vector2 size = {img->GetWidth(), img->GetHeight()};

			Vector2 scrSize = {renderer->ScreenWidth(), renderer->ScreenHeight()};
			Vector2 scrCenter = scrSize * 0.5F;

			float radius = scrSize.GetLength() * 0.5F;

			for (const auto& spr : hurtSprites) {
				float alpha = spr.strength - per;
				if (alpha < 0.0F)
					continue;
				if (alpha > 1.0F)
					alpha = 1.0F;

				float c = cosf(spr.angle);
				float s = sinf(spr.angle);

				Vector2 radDir = {c, s};
				Vector2 angDir = {-s, c};
				float siz = spr.scale * radius;
				Vector2 base = radDir * radius + scrCenter;
				Vector2 centVect = radDir * (-siz);
				Vector2 sideVect1 = angDir * (siz * 4.0F * (spr.horzShift));
				Vector2 sideVect2 = angDir * (siz * 4.0F * (spr.horzShift - 1.0F));

				Vector2 v1 = base + centVect + sideVect1;
				Vector2 v2 = base + centVect + sideVect2;
				Vector2 v3 = base + sideVect1;

				renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, alpha));
				renderer->DrawImage(img, v1, v2, v3, AABB2(0, 8, size.x, size.y));
			}
		}

		void Client::DrawScreenEffect(bool hurt, float fadeTime) {
			SPADES_MARK_FUNCTION();

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			Player& p = world->GetLocalPlayer().value();
			float hpper = p.GetHealth() / 100.0F;

			float lastEffectTime = hurt ? lastHurtTime : lastHealTime;
			float timeSinceEffect = time - lastEffectTime;

			if (time >= lastEffectTime && timeSinceEffect < fadeTime) {
				float prg = 1.0F - (timeSinceEffect / fadeTime);
				prg *= 0.3F + (1.0F - hpper) * 0.7F;
				prg = 1.0F - std::min(prg, 0.9F);

				if (dynamic_cast<draw::SWRenderer*>(&GetRenderer())) {
					prg = (1.0F - prg) * 0.5F;
					renderer->SetColorAlphaPremultiplied(hurt
						? MakeVector4(prg, 0.0F, 0.0F, prg)
						: MakeVector4(0.0F, prg, 0.0F, prg));
					renderer->DrawImage(nullptr, AABB2(0, 0, sw, sh));
				} else {
					renderer->MultiplyScreenColor(hurt
						? MakeVector3(1.0F, prg, prg)
						: MakeVector3(prg, 1.0F, prg));

					prg = (1.0F - prg) * 0.1F;
					renderer->SetColorAlphaPremultiplied(hurt
						? MakeVector4(prg, 0.0F, 0.0F, prg)
						: MakeVector4(0.0F, prg, 0.0F, prg));
					renderer->DrawImage(nullptr, AABB2(0, 0, sw, sh));
				}
			}
		}

		Vector4 Client::GetPlayerColor(Player& player) {
			Vector4 playerColor = MakeVector4(1, 1, 1, 1);

			Vector3 eye = lastSceneDef.viewOrigin;
			Vector3 dir = player.GetEye() - eye;

			float dist = dir.GetSquaredLength2D();

			float fadeStart = FOG_DISTANCE + 10.0F;
			float fadeEnd = fadeStart + 10.0F;
			float fade = (sqrtf(dist) - fadeStart) / (fadeEnd - fadeStart);
			float alpha = Clamp(1.0F - fade, 0.1F, 1.0F);

			// camera coincides with the player's eye (e.g. mid-spectator-switch);
			// skip the raycast since direction is undefined
			GameMap::RayCastResult mapResult;
			if (dir.GetSquaredLength() < 1.0e-6F) {
				mapResult.hit = false;
			} else {
				dir = dir.Normalize();
				mapResult = map->CastRay2(eye, dir, 256);
			}

			if (dist > FOG_DISTANCE_SQ) {
				playerColor = MakeVector4(1, 0.75, 0, 1);
			} else if (mapResult.hit && (mapResult.hitPos - eye).GetSquaredLength2D() < dist) {
				playerColor = player.IsLocalPlayer() ? MakeVector4(0, 1, 1, 1)
													 : ConvertColorRGBA(player.GetColor());
			}

			playerColor.w *= alpha;

			return playerColor;
		}

		void Client::DrawPlayerName(Player& player, const Vector4& color) {
			SPADES_MARK_FUNCTION();

			Vector3 origin = player.GetEye();
			origin.z -= 0.45F; // above player head

			Vector2 scrPos;
			if (Project(origin, scrPos)) {
				scrPos.x += (int)cg_playerNameX;
				scrPos.y += (int)cg_playerNameY;

				std::string str = player.GetName();

				// add distance
				const float dist = (origin - lastSceneDef.viewOrigin).GetLength2D();
				if ((int)cg_playerNames < 2 && dist <= FOG_DISTANCE) {
					char buf[32];
					snprintf(buf, sizeof(buf), " [%.1f]", dist);
					str += buf;
				}

				IFont& font = cg_smallFont
					? fontManager->GetSmallFont()
					: fontManager->GetGuiFont();

				Vector2 size = font.Measure(str);
				scrPos.x -= size.x * 0.5F;
				scrPos.y -= size.y;

				// rounded for better pixel alignment
				scrPos.x = floorf(scrPos.x);
				scrPos.y = floorf(scrPos.y);

				float luminosity = color.x + color.y + color.z;
				Vector4 shadowColor = (luminosity > 0.9F)
					? MakeVector4(0, 0, 0, 0.8F)
					: MakeVector4(1, 1, 1, 0.8F);

				shadowColor.w *= color.w;

				font.DrawShadow(str, scrPos, 1.0F, color, shadowColor);
			}
		}

		void Client::DrawHottrackedPlayerName() {
			SPADES_MARK_FUNCTION();

			auto hottracked = HotTrackedPlayer();
			if (hottracked) {
				Player& player = std::get<0>(*hottracked);
				DrawPlayerName(player, MakeVector4(1, 1, 1, 1));
			}
		}

		void Client::DrawPlayerBox(Player& player, const Vector4& color) {
			SPADES_MARK_FUNCTION();

			// get player aabb
			const auto& box = player.GetBox().Inflate(0.25F);

			// corners of the 3d box
			std::array<Vector3, 8> corners = {{
				{box.min.x, box.min.y, box.min.z},
				{box.min.x, box.max.y, box.min.z},
				{box.max.x, box.max.y, box.min.z},
				{box.max.x, box.min.y, box.min.z},
				{box.max.x, box.max.y, box.max.z},
				{box.min.x, box.max.y, box.max.z},
				{box.min.x, box.min.y, box.max.z},
				{box.max.x, box.min.y, box.max.z}
			}};

			float left = std::numeric_limits<float>::max();
			float top = std::numeric_limits<float>::max();
			float right = -std::numeric_limits<float>::max();
			float bottom = -std::numeric_limits<float>::max();

			// get screen positions
			Vector2 scrPos;
			for (auto const& corner : corners) {
				if (!Project(corner, scrPos))
					return; // offscreen or behind camera

				left = std::min(left, scrPos.x);
				right = std::max(right, scrPos.x);
				top = std::min(top, scrPos.y);
				bottom = std::max(bottom, scrPos.y);
			}

			// rounded for better pixel alignment
			left = floorf(left);
			top = floorf(top);
			right = ceilf(right);
			bottom = ceilf(bottom);

			// draw boxes
			Vector4 shadowP = MakeVector4(0, 0, 0, 0.25F * color.w);
			shadowP.x *= shadowP.w;
			shadowP.y *= shadowP.w;
			shadowP.z *= shadowP.w;

			Vector4 colorP = MakeVector4(color.x, color.y, color.z, 1.0F * color.w);
			colorP.x *= colorP.w;
			colorP.y *= colorP.w;
			colorP.z *= colorP.w;

			renderer->SetColorAlphaPremultiplied(shadowP);
			renderer->DrawOutlinedRect(left - 1, top - 1, right + 1, bottom + 1);

			renderer->SetColorAlphaPremultiplied(colorP);
			renderer->DrawOutlinedRect(left, top, right, bottom);
		}

		void Client::DrawPubOVL() {
			SPADES_MARK_FUNCTION();

			auto cameraMode = GetCameraMode();
			bool isFollowingNonLocal = FollowsNonLocalPlayer(cameraMode);

			int focusPlayerId = GetCameraTargetPlayerId();
			stmp::optional<Player&> maybeFocusPlayer;
			if (focusPlayerId >= 0
			 && static_cast<size_t>(focusPlayerId) < world->GetNumPlayerSlots())
				maybeFocusPlayer = world->GetPlayer(static_cast<unsigned int>(focusPlayerId));

			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue; // player is non-existent

				Player& p = maybePlayer.value();
				if (p.IsSpectator() || !p.IsAlive())
					continue; // don't draw dead players or spectators

				// dont draw the focused player name when following non-local players
				if (maybeFocusPlayer && &p == &maybeFocusPlayer.value() && isFollowingNonLocal)
					continue;

				// Do not draw a player with an invalid state
				if (p.GetFront().GetSquaredLength() < 0.01F)
					continue;

				const auto& color = GetPlayerColor(p);
				if (staffSpectating)
					DrawPlayerBox(p, color);
				if (spectatorPlayerNames)
					DrawPlayerName(p, color);
			}
		}

		void Client::DrawDeadPlayers() {
			SPADES_MARK_FUNCTION();

			Vector3 eye = lastSceneDef.viewOrigin;

			IFont& font = cg_smallFont
				? fontManager->GetSmallFont()
				: fontManager->GetGuiFont();

			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue; // player is non-existent

				Player& p = maybePlayer.value();
				if (p.IsAlive())
					continue;

				// Do not draw a player with an invalid state
				if (p.GetFront().GetSquaredLength() < 0.01F)
					continue;

				Vector3 origin = p.GetEye();
				origin.z -= 0.9F; // above player

				float dist = (origin - eye).GetLength();
				float fadeStart = 2.0F;
				float fadeEnd = fadeStart + 1.0F;
				float fade = (dist - fadeStart) / (fadeEnd - fadeStart);
				float alpha = Clamp(1.0F - fade, 0.0F, 1.0F);

				if (alpha <= 0.0F)
					continue;

				Vector2 scrPos;
				if (!Project(origin, scrPos))
					continue;

				std::string str =_Tr("Client", "{0}", p.GetName());

				Vector2 size = font.Measure(str);
				scrPos.x -= size.x * 0.5F;
				scrPos.y -= size.y;

				// rounded for better pixel alignment
				scrPos.x = floorf(scrPos.x);
				scrPos.y = floorf(scrPos.y);

				Vector4 playerColor = MakeVector4(1, 1, 1, 1);
				Vector4 shadowColor = MakeVector4(0, 0, 0, 0.8F);

				playerColor.w *= alpha;
				shadowColor.w *= alpha;

				font.DrawShadow(str, scrPos, 1.0F, playerColor, shadowColor);
			}
		}

		void Client::DrawDebugAim(Player& p) {
			SPADES_MARK_FUNCTION();

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			Weapon& weapon = p.GetWeapon();

			float spread = weapon.GetSpread();

			// halve spread when aiming
			float aimDownState = clientPlayers[p.GetId()]->GetAimDownState();
			if (aimDownState > 0.99F)
				spread /= 2;

			// halve spread when crouching (except for shotgun)
			PlayerInput actualInput = p.GetInput();
			WeaponType weaponType = weapon.GetWeaponType();
			if (actualInput.crouch && weaponType != SHOTGUN_WEAPON)
				spread /= 2;

			float fovY = tanf(lastSceneDef.fovY * 0.5F);
			float spreadDistance = (spread * 2) * (sh * 0.5F) / fovY;

			Vector2 center;
			center.x = sw * 0.5F;
			center.y = sh * 0.5F;

			Vector2 p1, p2;
			p1.x = (int)floorf(center.x - spreadDistance);
			p1.y = (int)floorf(center.y - spreadDistance);
			p2.x = (int)ceilf(center.x + spreadDistance);
			p2.y = (int)ceilf(center.y + spreadDistance);

			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 1));
			renderer->DrawOutlinedRect(p1.x - 1, p1.y - 1, p2.x + 1, p2.y + 1);

			renderer->SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1));
			renderer->DrawOutlinedRect(p1.x, p1.y, p2.x, p2.y);
		}

		void Client::DrawFirstPersonHUD() {
			SPADES_MARK_FUNCTION();

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			int playerId = GetCameraTargetPlayerId();
			Player& p = world->GetPlayer(playerId).value();

			clientPlayers[playerId]->Draw2D();

			if (cg_hitIndicator && hitFeedbackIconState > 0.0F) {
				Handle<IImage> img = renderer->RegisterImage("Gfx/HitFeedback.png");
				Vector2 size = {img->GetWidth(), img->GetHeight()};

				Vector4 color = hitFeedbackFriendly
					? MakeVector4(0.02F, 1, 0.02F, 1)
					: MakeVector4(1, 0.02F, 0.04F, 1);

				renderer->SetColorAlphaPremultiplied(color * hitFeedbackIconState);
				renderer->DrawImage(img, (MakeVector2(sw, sh) - size) * 0.5F);
			}

			if (cg_debugAim && p.IsToolWeapon())
				DrawDebugAim(p);
		}

		void Client::DrawJoinedAlivePlayerHUD() {
			SPADES_MARK_FUNCTION();

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			float ratio = (sw / sh);
			float safeZoneXMin = 0.75F;
			if (ratio < 0.26F)
				safeZoneXMin = 0.28F;
			else if (ratio < 0.56F)
				safeZoneXMin = 0.475F;

			float safeZoneX = Clamp((float)cg_hudSafezoneX, safeZoneXMin, 1.0F);
			float safeZoneY = Clamp((float)cg_hudSafezoneY, 0.85F, 1.0F);

			float x = (sw - 16.0F) * safeZoneX;
			float y = (sh - 16.0F) * safeZoneY;

			const int statsMode = cg_stats;
			if (statsMode == 1)
				y -= cg_statsSmallFont ? 2.0F : 12.0F;

			// rounded for better pixel alignment
			x = floorf(x);
			y = floorf(y);

			Player& p = world->GetLocalPlayer().value();
			Weapon& weapon = p.GetWeapon();

			Player::ToolType curToolType = p.GetTool();
			WeaponType curWeaponType = weapon.GetWeaponType();

			bool isToolWeapon = curToolType == Player::ToolWeapon;
			bool awaitingReload = weapon.IsAwaitingReloadCompletion();
			bool isReloading = awaitingReload && !weapon.IsReloadSlow();

			Handle<IImage> ammoIcon;
			float spacing = 2.0F;
			int clipNum, clipSize, stockNum, stockMax;

			IFont& squareFont = fontManager->GetSquareDesignFont();

			IntVector3 col;
			switch ((int)cg_hudColor) {
				case 1: // team color
					col = p.GetColor();
					col += (MakeIntVector3(255, 255, 255) - col) / 2;
					break;
				case 2: col = MakeIntVector3(120, 200, 255); break; // light blue
				case 3: col = MakeIntVector3(0, 100, 255); break; // blue
				case 4: col = MakeIntVector3(230, 100, 255); break; // purple
				case 5: col = MakeIntVector3(255, 50, 50); break; // red
				case 6: col = MakeIntVector3(255, 120, 50); break; // orange
				case 7: col = MakeIntVector3(255, 255, 0); break; // yellow
				case 8: col = MakeIntVector3(0, 255, 0); break; // green
				case 9: col = MakeIntVector3(0, 255, 120); break; // aqua
				case 10: col = MakeIntVector3(255, 120, 150); break; // pink
				default: // custom
					col.x = (int)cg_hudColorR;
					col.y = (int)cg_hudColorG;
					col.z = (int)cg_hudColorB;
					break;
			}

			Vector4 color = ConvertColorRGBA(col);
			float luminosity = color.x + color.y + color.z;
			Vector4 shadowColor = (luminosity > 0.9F)
				? MakeVector4(0, 0, 0, 0.5)
				: MakeVector4(1, 1, 1, 0.5);

			// premultiplied
			Vector4 shadowP = shadowColor;
			shadowP.x *= shadowP.w;
			shadowP.y *= shadowP.w;
			shadowP.z *= shadowP.w;

			Vector4 white = MakeVector4(1, 1, 1, 1);
			Vector4 red = MakeVector4(1, 0, 0, 1);
			Vector4 gray = MakeVector4(0.4F, 0.4F, 0.4F, 1);
			Vector4 bgColor = (luminosity > 0.9F) ? color * gray : gray;

			switch (curToolType) {
				case Player::ToolSpade:
				case Player::ToolBlock:
					clipNum = stockNum = p.GetNumBlocks();
					clipSize = stockMax = 50;
					break;
				case Player::ToolGrenade:
					clipNum = stockNum = p.GetNumGrenades();
					clipSize = stockMax = 3;
					break;
				case Player::ToolWeapon: {
					switch (curWeaponType) {
						case RIFLE_WEAPON:
							ammoIcon = renderer->RegisterImage("Gfx/Bullet/7.62mm.png");
							break;
						case SMG_WEAPON:
							ammoIcon = renderer->RegisterImage("Gfx/Bullet/9mm.png");
							spacing = 1.0F;
							break;
						case SHOTGUN_WEAPON:
							ammoIcon = renderer->RegisterImage("Gfx/Bullet/12gauge.png");
							break;
						default: SPInvalidEnum("weapon.GetWeaponType()", curWeaponType);
					}

					clipSize = weapon.GetClipSize();
					clipNum = std::min(weapon.GetAmmo(), clipSize);
					stockNum = weapon.GetStock();
					stockMax = weapon.GetMaxStock();
				} break;
				default: SPInvalidEnum("p.GetTool()", curToolType);
			}

			// draw damage rings
			hurtRingView->Draw();

			// draw color palette
			if (curToolType == Player::ToolBlock)
				paletteView->Draw();

			// draw hotbar when unable to use tool
			if (cg_hudHotbar && (!CanLocalPlayerUseTool() || (isToolWeapon && isReloading))) {
				IFont& font = fontManager->GetSmallFont();

				// register tool icons
				Handle<IImage> blockIcon = renderer->RegisterImage("Gfx/Hotbar/Block.png");
				Handle<IImage> grenadeIcon = renderer->RegisterImage("Gfx/Hotbar/Grenade.png");
				Handle<IImage> spadeIcon = renderer->RegisterImage("Gfx/Hotbar/Spade.png");
				Handle<IImage> weaponIcon;
				switch (curWeaponType) {
					case RIFLE_WEAPON:
						weaponIcon = renderer->RegisterImage("Gfx/Hotbar/Rifle.png");
						break;
					case SMG_WEAPON:
						weaponIcon = renderer->RegisterImage("Gfx/Hotbar/SMG.png");
						break;
					case SHOTGUN_WEAPON:
						weaponIcon = renderer->RegisterImage("Gfx/Hotbar/Shotgun.png");
						break;
				}

				const int toolCount = 4;
				Handle<IImage> toolIcons[toolCount] = {
					spadeIcon, blockIcon,
					weaponIcon, grenadeIcon
				};
				std::string hotKeys[toolCount] = {
					cg_keyToolSpade, cg_keyToolBlock,
					cg_keyToolWeapon, cg_keyToolGrenade
				};

				const float iconHeight = 20.0F;
				const float iconSpacing = 10.0F;

				float totalWidth = 0.0F;
				for (int i = 0; i < toolCount; i++) {
					if (!p.IsToolSelectable(static_cast<Player::ToolType>(i)))
						continue;
					totalWidth += toolIcons[i]->GetWidth() + iconSpacing;
				}

				Vector2 iconPos = MakeVector2((sw - totalWidth) * 0.5F, sh * 0.6F);

				// rounded for better pixel alignment
				iconPos.x = floorf(iconPos.x);
				iconPos.y = floorf(iconPos.y);

				for (int i = 0; i < toolCount; i++) {
					const auto tool = static_cast<Player::ToolType>(i);
					if (!p.IsToolSelectable(tool))
						continue;

					// draw icon
					Handle<IImage> icon = toolIcons[i];
					float iconWidth = icon->GetWidth();
					Vector2 pos = iconPos;

					Vector4 iconColor = color;
					Vector4 iconShadow = shadowP;

					// fade non-selected icons
					if (tool != curToolType) {
						iconColor *= 0.25F;
						iconShadow *= 0.25F;
					} else {
						float state = 1.0F - hotBarIconState;
						pos.y -= floorf(10.0F * state);
					}

					renderer->SetColorAlphaPremultiplied(iconShadow);
					renderer->DrawImage(icon, pos + MakeVector2(1, 1));

					renderer->SetColorAlphaPremultiplied(iconColor);
					renderer->DrawImage(icon, pos);

					// draw hotkey
					Vector4 hotKeyColor = MakeVector4(1, 1, 1, 1);
					Vector4 hotKeyShadow = MakeVector4(0, 0, 0, 0.8F);
					std::string hotkeyStr = hotKeys[i];
					Vector2 hotkeyPos = iconPos + MakeVector2(iconWidth - 1.0F, iconHeight - 1.0F);
					font.DrawOutline(hotkeyStr, hotkeyPos, 1.0F, hotKeyColor, hotKeyShadow);

					iconPos.x += iconWidth + iconSpacing;
				}
			}

			// if the player has the intel, display an intel icon
			stmp::optional<IGameMode&> mode = world->GetMode();
			if (mode && mode->ModeType() == IGameMode::m_CTF) {
				auto& ctf = static_cast<CTFGameMode&>(mode.value());
				if (ctf.PlayerHasIntel(p)) {
					Handle<IImage> img = renderer->RegisterImage("Gfx/Intel.png");
					Vector2 size = {img->GetWidth(), img->GetHeight()};
					Vector2 pos = MakeVector2(((sw - size.x) * 0.5F) - 64.0F, y - size.y);

					// Strobe
					float pulse = fabsf(sinf(time * 2.0F));
					pulse *= pulse;

					renderer->SetColorAlphaPremultiplied(shadowP * pulse);
					renderer->DrawImage(img, pos + MakeVector2(1, 1));

					renderer->SetColorAlphaPremultiplied(color * pulse);
					renderer->DrawImage(img, pos);
				}
			}

			// draw "press ... to reload"
			if (isToolWeapon) {
				std::string msg = "";
				if (awaitingReload) {
					msg = _Tr("Client", "Reloading");
				} else if (stockNum > 0 && clipNum < (clipSize / 4)) {
					msg = _Tr("Client", "Press [{0}] to Reload", TrKey(cg_keyReloadWeapon));
				}

				if (!msg.empty()) {
					IFont& font = fontManager->GetGuiFont();
					Vector2 size = font.Measure(msg);
					Vector2 pos = MakeVector2((sw - size.x) * 0.5F, sh * (2.0F / 3.0F));
					font.DrawShadow(msg, pos, 1.0F, white, MakeVector4(0, 0, 0, 0.5F));
				}
			}

			// draw remaining ammo counter
			{
				bool drawIcon = (int)cg_hudAmmoStyle < 1;

				float per = Clamp((float)clipNum / (float)(clipSize / 3), 0.0F, 1.0F);
				Vector4 ammoCol = color + (red - color) * (1.0F - per);

				per = Clamp((float)stockNum / (float)(stockMax / 3), 0.0F, 1.0F);
				Vector4 stockCol = color + (red - color) * (1.0F - per);

				auto stockStr = ToString(stockNum);
				if (!drawIcon && isToolWeapon)
					stockStr = ToString(clipNum) + "-" + stockStr;

				IFont& font = squareFont;
				Vector2 size = font.Measure(stockStr);
				Vector2 pos = MakeVector2(x, y) - size;

				// draw ammo icon
				if (drawIcon && isToolWeapon) {
					Vector2 iconSize = MakeVector2(ammoIcon->GetWidth(), ammoIcon->GetHeight());
					Vector2 iconPos = MakeVector2(x - (iconSize.x + spacing), y - iconSize.y);

					float reloadPrg = weapon.GetReloadProgress();
					int clip = isReloading ? (int)(clipSize * reloadPrg) : clipSize;

					for (int i = 0; i < clipSize; i++) {
						iconPos.x = x - ((float)(i + 1) * (iconSize.x + spacing));

						// draw icon shadow
						renderer->SetColorAlphaPremultiplied(shadowP);
						renderer->DrawImage(ammoIcon, iconPos + MakeVector2(1, 1));

						// draw icon
						renderer->SetColorAlphaPremultiplied(
						  isReloading ? ((i < clip) ? color : bgColor)
									  : ((clipNum >= i + 1) ? ammoCol : bgColor));
						renderer->DrawImage(ammoIcon, iconPos);
					}

					pos.y -= iconSize.y + 5.0F;
				}

				font.Draw(stockStr, pos + MakeVector2(1, 1), 1.0F, shadowColor);
				font.Draw(stockStr, pos, 1.0F, drawIcon ? stockCol : ammoCol);
			}

			// draw player health
			{
				int hp = lastHealth; // player health
				int maxHealth = 100; // server doesn't send this
				int dmgTaken = damageTaken;

				float hpFrac = Clamp((float)hp / (float)maxHealth, 0.0F, 1.0F);
				float dmgFrac = Clamp((float)dmgTaken / (float)maxHealth, 0.0F, 1.0F);

				Vector4 hpColor = color + (red - color) * (1.0F - hpFrac);

				float timeSinceLastHurt = time - lastHurtTime;
				float hurtTime = timeSinceLastHurt / 0.25F;
				hurtTime = std::max(0.0F, 1.0F - hurtTime);

				const float fadeOutStart = 0.4F;
				const float fadeDuration = 0.3F;
				float fade = timeSinceLastHurt - fadeOutStart;
				fade = 1.0F - (fade / fadeDuration);
				fade = Clamp(fade, 0.0F, 1.0F);

				auto healthStr = ToString(cg_hudHealthAnimation
					? ((int)(hp + dmgTaken * hurtTime)) : hp);
				IFont& font = squareFont;
				Vector2 size = font.Measure(healthStr);
				Vector2 pos = MakeVector2(sw - x, y - size.y);

				if (cg_hudHealthBar) {
					float barW = 45.0F;
					float barH = 4.0F;
					float barX = sw - x;
					float barY = y - barH;
					float barPrg = std::max(1.0F, floorf(barW * hpFrac));

					Handle<IImage> img = renderer->RegisterImage("Gfx/White.tga");

					// draw shadow
					renderer->SetColorAlphaPremultiplied(shadowP);
					renderer->DrawImage(img, AABB2(barX + 1, barY + 1, barW, barH));

					// draw background
					if (hp < maxHealth) {
						renderer->SetColorAlphaPremultiplied(bgColor);
						renderer->DrawImage(img, AABB2(barX, barY, barW, barH));

						// draw damaged portion
						if (fade > 0.0F) {
							Vector4 dmgColor = red + (white - red) * hurtTime;
							float dmgBarW = std::min(ceilf(barW * dmgFrac), barW - barPrg);
							for (float x2 = barPrg; x2 < barPrg + dmgBarW; x2 += 1.0F) {
								float t = (x2 - barPrg) / dmgBarW;
								float per = (1.0F - t * t) * fade;
								renderer->SetColorAlphaPremultiplied(dmgColor * per);
								renderer->DrawImage(img, AABB2(barX + x2, barY, 1.0F, barH));
							}
						}
					}

					// draw health bar
					renderer->SetColorAlphaPremultiplied(color + (white - color) * hurtTime);
					renderer->DrawImage(img, AABB2(barX, barY, barPrg, barH));

					pos.y -= barH + 5.0F;
				}

				font.Draw(healthStr, pos + MakeVector2(1, 1), 1.0F, shadowColor);
				font.Draw(healthStr, pos, 1.0F, hpColor + (white - hpColor) * hurtTime);
			}
		}

		void Client::DrawHitTestDebugger() {
			SPADES_MARK_FUNCTION();

			auto* debugger = world->GetHitTestDebugger();
			if (!debugger)
				return;

			auto bmp = debugger->GetBitmap();
			if (bmp) {
				auto img = renderer->CreateImage(*bmp);
				debugHitTestImage.Set(img.GetPointerOrNull());
			}

			if (!debugHitTestImage)
				return;

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			float cfgWndSize = cg_dbgHitTestSize;
			Vector2 wndSize = {cfgWndSize, cfgWndSize};

			Vector2 zoomedSize = {512, 512};
			if (sw < zoomedSize.x || sh < zoomedSize.y)
				zoomedSize *= 0.75F;

			if (debugHitTestZoom) {
				float per = debugHitTestZoomState;
				per = 1.0F - per;
				per *= per;
				per = 1.0F - per;
				per = Mix(0.0F, 1.0F, per);
				zoomedSize = Mix(wndSize, zoomedSize, per);
				wndSize = zoomedSize;
			}

			float winX = (sw - 8.0F) - wndSize.x;
			float winY = (sh - 8.0F) - wndSize.y - 64.0F;

			const int statsMode = cg_stats;
			if (statsMode == 1)
				winY -= cg_statsSmallFont ? 2.0F : 12.0F;

			AABB2 inRect(128, 512 - 128, 256, 256 - 512); // flip Y axis
			AABB2 outRect(winX, winY, wndSize.x, wndSize.y);
			if (debugHitTestZoom) {
				outRect.min = MakeVector2(sw - zoomedSize.x, sh - zoomedSize.y) * 0.5F;
				outRect.max = MakeVector2(sw + zoomedSize.x, sh + zoomedSize.y) * 0.5F;
			}

			const float fadeOutStart = cg_dbgHitTestFadeTime;
			const float fadeDuration = 1.0F;
			float timeSinceLastHit = world->GetTime() - lastHitTime;
			float fade = timeSinceLastHit - fadeOutStart;
			fade = 1.0F - (fade / fadeDuration);
			fade = Clamp(fade, 0.0F, 1.0F);

			float alpha = debugHitTestZoom ? debugHitTestZoomState : fade;
			if (alpha <= 0.0F)
				return;

			renderer->SetColorAlphaPremultiplied(MakeVector4(alpha, alpha, alpha, alpha));
			renderer->DrawImage(debugHitTestImage, outRect, inRect);

			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, alpha));
			renderer->DrawOutlinedRect(outRect.min.x - 1, outRect.min.y - 1, outRect.max.x + 1, outRect.max.y + 1);
		}

		void Client::DrawPlayerStats() {
			SPADES_MARK_FUNCTION();

			Player& p = world->GetLocalPlayer().value();

			IFont& font = cg_smallFont
				? fontManager->GetSmallFont()
				: fontManager->GetGuiFont();

			float sh = renderer->ScreenHeight();

			float x = 8.0F;
			float y = sh * 0.5F;
			y -= (float)cg_playerStatsHeight;

			Vector4 color = MakeVector4(1, 1, 1, 1);
			Vector4 shadow = MakeVector4(0, 0, 0, 0.7F);

			float lh = cg_smallFont ? 14.0F : 20.0F;
			auto addLine = [&](const std::string& text) {
				Vector2 pos = MakeVector2(x, y);
				y += lh;
				font.DrawShadow(text, pos, 1.0F, color, shadow);
			};

			const auto& weaponType = p.GetWeaponType();
			int hits = weaponStats.hits[weaponType];
			int shots = weaponStats.shots[weaponType];
			int accPerc = int(100.0F * (float(hits) / float(std::max(1, shots))));
			addLine(_Tr("Client", "Accuracy: {0}%", accPerc));

			char buf[32];
			snprintf(buf, sizeof(buf), "%.3g", curKills / float(std::max(1, curDeaths)));
			addLine(_Tr("Client", "Kill/Death Ratio: {0}", std::string(buf)));
			addLine(_Tr("Client", "Kill Streak: {0}, Best: {1}", curStreak, bestStreak));
			addLine(_Tr("Client", "Melee Kills: {0}", meleeKills));
			addLine(_Tr("Client", "Grenade Kills: {0}", grenadeKills));

			if (cg_playerStatsShowPlacedBlocks && !activeNet->GetGameProperties()->isGameModeArena)
				addLine(_Tr("Client", "Blocks Placed: {0}", placedBlocks));
		}

		void Client::UpdateDamageIndicators(float dt) {
			for (auto it = damageIndicators.begin();
				 it != damageIndicators.end();) {
				DamageIndicator& ent = *it;
				ent.fade -= dt;
				if (ent.fade < 0) {
					std::list<DamageIndicator>::iterator tmp = it++;
					damageIndicators.erase(tmp);
					continue;
				}

				ent.position += ent.velocity * dt;

				++it;
			}
		}

		void Client::DrawDamageIndicators() {
			SPADES_MARK_FUNCTION();

			IFont& mediumFont = fontManager->GetMediumFont();
			IFont& guiFont = fontManager->GetGuiFont();

			for (const auto& dmg : damageIndicators) {
				float fade = dmg.fade;
				if (fade > 1.0F)
					fade = 1.0F;

				Vector2 scrPos;
				if (Project(dmg.position, scrPos)) {
					bool crit = dmg.crit;
					IFont& font = crit ? mediumFont : guiFont;

					int damage = dmg.damage;
					auto damageStr = ToString(damage);
					Vector2 size = font.Measure(damageStr);
					scrPos -= size * 0.5F;

					// rounded for better pixel alignment
					scrPos.x = floorf(scrPos.x);
					scrPos.y = floorf(scrPos.y);

					float per = Clamp(damage / 100.0F, 0.0F, 1.0F);
					Vector4 shadow = MakeVector4(0, 0, 0, 0.4F * fade);
					Vector4 color = MakeVector4(1.0F, 1.0F - per, 0.0F, fade);

					if (crit) {
						float pulse = (sinf((time - dmg.lastHitTime) * 10.0F) * 0.5F) + 0.5F;
						color = MakeVector4(1.0F, pulse * 0.8F, pulse * 0.2F, fade);
					}

					font.DrawShadow(damageStr, scrPos + MakeVector2(1, 1), 1.0F, shadow, shadow);
					font.Draw(damageStr, scrPos, 1.0F, color);
				}
			}
		}

		void Client::DrawDeadPlayerHUD() {
			SPADES_MARK_FUNCTION();

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			// draw respawn time
			std::string msg = (lastRespawnCount > 0)
				? _Tr("Client", "You will respawn in: {0}", lastRespawnCount)
				: _Tr("Client", "Waiting for respawn");

			Vector4 color = MakeVector4(1, 1, 1, 1);
			Vector4 shadowColor = MakeVector4(0, 0, 0, 0.5F);

			IFont& font = fontManager->GetGuiFont();
			Vector2 size = font.Measure(msg);
			Vector2 pos = MakeVector2((sw - size.x) * 0.5F, sh / 2.5F);
			font.DrawShadow(msg, pos, 1.0F, color, shadowColor);

			// draw deaths count
			const float fadeOutStart = 1.3F;
			const float fadeDuration = 1.0F;
			float timeSinceDeath = time - lastAliveTime;
			float fade = timeSinceDeath - fadeOutStart;
			fade = 1.0F - (fade / fadeDuration);
			fade = Clamp(fade, 0.0F, 1.0F);

			if (fade > 0.0F) {
				color.w = fade;
				shadowColor.w = 0.5F * fade;

				msg = ToString(curDeaths);
				IFont& bigFont = fontManager->GetLargeFont();
				size = bigFont.Measure(msg);
				pos.x = (sw - size.x) * 0.5F;
				pos.y += 30.0F;
				bigFont.DrawShadow(msg, pos, 1.0F, color, shadowColor);
			}
		}

		void Client::DrawSpectateHUD() {
			SPADES_MARK_FUNCTION();

			IFont& font = cg_smallFont
				? fontManager->GetSmallFont()
				: fontManager->GetGuiFont();

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			stmp::optional<Player&> maybePlayer = world->GetLocalPlayer();

			// In demo mode there's no local player, treat as spectator
			bool localPlayerIsSpectator = IsDemoMode() || staffSpectating ||
				(maybePlayer && maybePlayer->IsSpectator());

			float x = sw - 8.0F;
			float minY = sh * 0.5F;
			minY -= 64.0F;

			float y = cg_minimapSize;
			if (y < minY)
				y = minY;
			if (y > 256.0F)
				y = 256.0F;
			y += 32.0F;

			Vector4 color = MakeVector4(1, 1, 1, 1);
			Vector4 shadow = MakeVector4(0, 0, 0, 0.7F);

			float lh = cg_smallFont ? 14.0F : 20.0F;
			auto addLine = [&](const std::string& text) {
				Vector2 pos = MakeVector2(x, y);
				pos.x -= font.Measure(text).x;
				y += lh;
				font.DrawShadow(text, pos, 1.0F, color, shadow);
			};

			auto cameraMode = GetCameraMode();

			int playerId = GetCameraTargetPlayerId();
			auto maybeCamTarget = world->GetPlayer(playerId);

			// Help messages (make sure to synchronize these with the keyboard input handler)
			if (FollowsNonLocalPlayer(cameraMode) && maybeCamTarget) {
				Player& camTarget = maybeCamTarget.value();
				if (HasTargetPlayer(cameraMode)) {
					addLine(_Tr("Client", "Following {0} [#{1}]",
						world->GetPlayerName(playerId), playerId));

					int secs = (int)roundf(camTarget.GetTimeToRespawn());
					if (secs > 0)
						addLine(_Tr("Client", "Respawning in: {0}", secs));
				}

				y += lh * 0.5F;

				if (camTarget.IsAlive())
					addLine(_Tr("Client", "[{0}] Cycle camera mode", TrKey(cg_keyJump)));

				addLine(_Tr("Client", "[{0}/{1}] Next/Prev player",
					TrKey(cg_keyAttack), TrKey(cg_keyAltAttack)));

				if (localPlayerIsSpectator)
					addLine(_Tr("Client", "[{0}] Unfollow", TrKey(cg_keyReloadWeapon)));
			} else if (!FollowsNonLocalPlayer(cameraMode)) {
				addLine(_Tr("Client", "[{0}/{1}] Follow a player",
					TrKey(cg_keyAttack), TrKey(cg_keyAltAttack)));
			}

			if (cameraMode == ClientCameraMode::Free)
				addLine(_Tr("Client", "[{0}/{1}] Go up/down",
					TrKey(cg_keyJump), TrKey(cg_keyCrouch)));

			if (localPlayerIsSpectator) {
				addLine(_Tr("Client", "[{0}] Toggle player names",
					TrKey(cg_keyToggleSpectatorNames)));
				bool isStaff = activeNet && activeNet->GetGameProperties()->isStaff;
				if (isStaff || IsDemoMode())
					addLine(_Tr("Client", "[{0}] Toggle ESP",
						TrKey(cg_keyStaffSpectating)));
			}

			y += lh * 0.5F;

			if (IsDemoMode()) {
				// Demo playback controls
				std::string pauseLabel = (demoNet && demoNet->IsPaused())
					? _Tr("Client", "[{0}] Resume", TrKey(cg_keyDemoPlayPause))
					: _Tr("Client", "[{0}] Pause", TrKey(cg_keyDemoPlayPause));
				addLine(pauseLabel);
				addLine(_Tr("Client", "[{0}/{1}] Seek +/-5s",
					TrKey(cg_keyDemoSeekForward), TrKey(cg_keyDemoSeekBackward)));

				char speedBuf[16];
				float spd = demoNet ? demoNet->GetSpeed() : 1.0f;
				snprintf(speedBuf, sizeof(speedBuf), "%.2gx", spd);
				addLine(_Tr("Client", "[{0}/{1}] Speed: {2}",
					TrKey(cg_keyDemoSlowDown), TrKey(cg_keyDemoSpeedUp), std::string(speedBuf)));
			} else if (!inGameLimbo) {
				addLine(_Tr("Client", "[{0}] Select Team/Weapon", TrKey(cg_keyLimbo)));
			}
		}

		void Client::DrawBlockPaletteHUD(float winY) {
			SPADES_MARK_FUNCTION();

			float sw = renderer->ScreenWidth();

			IFont& font = cg_smallFont
				? fontManager->GetSmallFont()
				: fontManager->GetGuiFont();

			std::vector<std::string> lines;
			lines.push_back(_Tr("Client", "[{0}] Grab color", TrKey(cg_keyCaptureColor)));
			lines.push_back(_Tr("Client", "[{0}/{1}] Navigate up/down",
				TrKey(cg_keyPaletteUp), TrKey(cg_keyPaletteDown)));
			lines.push_back(_Tr("Client", "[{0}/{1}] Navigate left/right",
				TrKey(cg_keyPaletteLeft), TrKey(cg_keyPaletteRight)));
			lines.push_back(_Tr("Client", "[{0}] Toggle extended palette", TrKey(cg_keyExtendedPalette)));

			// add color information
			if ((int)cg_hudPalette >= 2) {
				IntVector3 color = world->GetLocalPlayer()->GetBlockColor();

				char buf[8];
				snprintf(buf, sizeof(buf), "%02X%02X%02X", color.x, color.y, color.z);
				lines.push_back(_Tr("Client", "#{0} / RGB({1}, {2}, {3})",
					std::string(buf), color.x, color.y, color.z));
			}

			float lh = cg_smallFont ? 14.0F : 20.0F;
			float totalHeight = (int)lines.size() * lh;

			float x = sw - 8.0F;
			float y = (winY - 8.0F) - totalHeight;

			Vector4 color = MakeVector4(1, 1, 1, 1);
			Vector4 shadow = MakeVector4(0, 0, 0, 0.7F);

			// draw each line
			for (const auto& line : lines) {
				Vector2 pos = MakeVector2(x, y);
				pos.x -= font.Measure(line).x;
				font.DrawShadow(line, pos, 1.0F, color, shadow);
				y += lh;
			}
		}

		void Client::DrawAlert() {
			SPADES_MARK_FUNCTION();

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			float fade = time - alertDisappearTime;
			fade = std::min(1.0F - fade, 1.0F);
			if (fade <= 0.0F)
				return;

			float borderFade = (time - alertAppearTime) / 0.5F;
			borderFade = Clamp(1.0F - borderFade, 0.0F, 1.0F);

			Handle<IImage> alertIcon = renderer->RegisterImage("Gfx/AlertIcon.png");

			IFont& font = fontManager->GetGuiFont();
			Vector2 textSize = font.Measure(alertContents);
			Vector2 contentsSize = textSize;
			contentsSize.y = std::max(contentsSize.y, 16.0F);
			if (alertType != AlertType::Notice)
				contentsSize.x += 22.0F;

			// add margin
			const float margin = 4.0F;
			contentsSize += margin * 2.0F;
			contentsSize = contentsSize.Floor(); // rounded

			Vector2 pos = MakeVector2(sw, sh) - contentsSize;
			pos *= MakeVector2(0.5F, 0.7F);
			pos.y += 40.0F;
			pos = pos.Floor(); // rounded

			Vector4 shadowColor = MakeVector4(0, 0, 0, 0.5F);
			Vector4 color = MakeVector4(0, 0, 0, 1);
			switch (alertType) {
				case AlertType::Notice: color = MakeVector4(0, 0, 0, 1); break;
				case AlertType::Warning: color = MakeVector4(1, 1, 0, 1); break;
				case AlertType::Error: color = MakeVector4(1, 0, 0, 1); break;
			}

			float x = pos.x - margin;
			float y = pos.y;
			float w = pos.x + contentsSize.x + margin;
			float h = pos.y + contentsSize.y;

			// draw background
			renderer->SetColorAlphaPremultiplied(shadowColor * fade);
			renderer->DrawFilledRect(x + 1, y + 1, w - 1, h - 1);

			// draw border
			renderer->SetColorAlphaPremultiplied(color * fade * (1.0F - borderFade));
			renderer->DrawOutlinedRect(x, y, w, h);

			// draw fading border
			if (borderFade > 0.0F) {
				float scale = 8.0F * (1.0F - borderFade);
				renderer->SetColorAlphaPremultiplied(color * borderFade);
				renderer->DrawOutlinedRect(x - scale, y - scale, w + scale, h + scale);
			}

			// draw alert icon
			if (alertType != AlertType::Notice) {
				Vector2 iconPos = pos;
				iconPos.x += margin;
				iconPos.y += (contentsSize.y - 16.0F) * 0.5F;

				renderer->SetColorAlphaPremultiplied(color * fade);
				renderer->DrawImage(alertIcon, iconPos);
			}

			// draw text
			Vector2 textPos = pos;
			textPos.x += (contentsSize.x - textSize.x) - margin;
			textPos.y += (contentsSize.y - textSize.y) * 0.5F;

			color = MakeVector4(1, 1, 1, fade);
			shadowColor.w = 0.5F * fade;

			font.DrawShadow(alertContents, textPos, 1.0F, color, shadowColor);
		}

		void Client::Draw2DWithWorld() {
			SPADES_MARK_FUNCTION();

			bool shouldDrawHUD = hudVisible && !cg_hideHud;

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			// fade the map when the world starts (draw)
			const float fadeOutStart = 1.0F;
			const float fadeDuration = 2.5F;
			float timeSinceWorldStart = time - worldSetTime;
			float fade = timeSinceWorldStart - fadeOutStart;
			fade = 1.0F - (fade / fadeDuration);
			fade = Clamp(fade, 0.0F, 1.0F);

			if (fade > 0.0F) {
				renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, fade));
				renderer->DrawImage(nullptr, AABB2(0, 0, sw, sh));
			}

			for (const auto& ent : localEntities)
				ent->Render2D();

			stmp::optional<Player&> maybePlayer = world->GetLocalPlayer();

			if (maybePlayer) { // joined local player
				Player& player = maybePlayer.value();

				if (cg_playerNamesDead)
					DrawDeadPlayers();

				bool localPlayerIsSpectator = player.IsSpectator() || staffSpectating;
				if (!localPlayerIsSpectator) {
					if (cg_hurtScreenEffects) {
						DrawHurtSprites();
						DrawScreenEffect(true);
					}

					if (cg_healScreenEffects)
						DrawScreenEffect(false);

					if (cg_playerNames)
						DrawHottrackedPlayerName();

					if (cg_damageIndicators)
						DrawDamageIndicators();
				} else if (spectatorPlayerNames) { // only if we are spectating
					DrawPubOVL();
				}

				if (IsFirstPerson(GetCameraMode()))
					DrawFirstPersonHUD();

				if (shouldDrawHUD) {
					tcView->Draw();

					if (cg_hudPlayerCount)
						DrawAlivePlayersCount();

					// draw map
					bool largeMap = largeMapView->IsZoomed();
					if (!largeMap)
						mapView->Draw();

					if (!localPlayerIsSpectator) { // player is not spectator
						if (cg_playerStats)
							DrawPlayerStats();
						if (!player.IsToolBlock() && !debugHitTestZoom)
							DrawHitTestDebugger();

						if (player.IsAlive()) {
							DrawJoinedAlivePlayerHUD();
						} else {
							DrawDeadPlayerHUD();
							DrawSpectateHUD();
						}
					} else {
						DrawSpectateHUD();
					}

					chatWindow->Draw();
					killfeedWindow->Draw();

					if (debugHitTestZoom)
						DrawHitTestDebugger();

					// large map view should come in front
					if (largeMap)
						largeMapView->Draw();
				} else if (AcceptsTextInput() || chatWindow->IsExpanded()) {
					// chat bypass cg_hideHud
					chatWindow->Draw();
				}

				DrawAlert();
				centerMessageView->Draw();
				if (scoreboardVisible) {
					scoreboard->Draw();
					DrawPlayingTime();
				}

				if (pieMenuView && pieMenuView->IsOpen())
					pieMenuView->Draw();

				// --- end "player is there" render
			} else {
				// world exists, but no local player: not joined (or demo mode)

				if (IsDemoMode() && shouldDrawHUD) {
					// Draw spectator HUD elements in demo mode.
					// Boxes always render (staff use); names follow the toggle.
					DrawPubOVL();

					tcView->Draw();

					if (cg_hudPlayerCount)
						DrawAlivePlayersCount();

					// Draw map
					bool largeMap = largeMapView->IsZoomed();
					if (!largeMap)
						mapView->Draw();

					// When following a player in first-person, draw their weapon
					// skin's 2D layer (crosshair / iron sights / scope).
					if (IsFirstPerson(GetCameraMode())) {
						int targetId = GetCameraTargetPlayerId();
						auto maybeTarget = world->GetPlayer(targetId);
						if (maybeTarget && maybeTarget->IsAlive())
							clientPlayers[targetId]->Draw2D();
					}

					DrawSpectateHUD();

					chatWindow->Draw();
					killfeedWindow->Draw();

					// Large map view should come in front
					if (largeMap)
						largeMapView->Draw();
				}

				// In demo mode, only show scoreboard when toggled
				if (!IsDemoMode() || scoreboardVisible) {
					scoreboard->Draw();
					DrawPlayingTime();
				}
				centerMessageView->Draw();
				DrawAlert();
			}

			if (cg_stats && shouldDrawHUD)
				DrawStats();

			DrawDemoPlaybackHUD();
			DrawRecordingIndicator();

			// draw limbo view (above everything)
			if (IsLimboViewActive() && !scriptedUI->NeedsInput())
				limbo->Draw();
		}

		void Client::Draw2DWithoutWorld() {
			SPADES_MARK_FUNCTION();

			DrawSplash();

			// no world; loading?
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			float prgBarW = 440.0F;
			float prgBarH = 8.0F;
			float prgBarX = (sw - prgBarW) * 0.5F;
			float prgBarY = sh - (prgBarH + 40.0F);

			// draw background bar
			renderer->SetColorAlphaPremultiplied(MakeVector4(0.2F, 0.2F, 0.2F, 1));
			renderer->DrawImage(nullptr, AABB2(prgBarX, prgBarY, prgBarW, prgBarH));

			// draw progress bar
			NetClientStatus status = activeNet->GetStatus();
			if (status == NetClientStatusReceivingMap) {
				float progress = mapReceivingProgressSmoothed;
				float prgBarMaxWidth = prgBarW * progress;

				renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0.5, 1, 1));
				renderer->DrawImage(nullptr, AABB2(prgBarX, prgBarY, prgBarMaxWidth, prgBarH));
				for (float x = 0.0F; x < prgBarMaxWidth; x += 1.0F) {
					float per = 1.0F - (x / prgBarMaxWidth);
					renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.5) * per);
					renderer->DrawImage(nullptr, AABB2(prgBarX + x, prgBarY, 1.0F, prgBarH));
				}
			} else { // draw indeterminate progress bar
				const float progressPosition = fmodf(timeSinceInit * 0.7F, 1.0F);
				const float centerX = progressPosition * (prgBarW + 400.0F) - 200.0F;
				for (float x = 0.0F; x < prgBarW; x += 1.0F) {
					float opacity = 1.0F - fabsf(x - centerX) / 200.0F;
					opacity = std::max(opacity, 0.0F) * 0.5F + 0.05F;
					renderer->SetColorAlphaPremultiplied(MakeVector4(0.5, 0.5, 0.5, 1) * opacity);
					renderer->DrawImage(nullptr, AABB2(prgBarX + x, prgBarY, 1.0F, prgBarH));
				}
			}

			// draw net status
			auto statusStr = activeNet->GetStatusString();
			IFont& font = fontManager->GetGuiFont();
			Vector2 size = font.Measure(statusStr);
			Vector2 pos = MakeVector2((sw - size.x) * 0.5F, (prgBarY - 10.0F) - size.y);
			font.Draw(statusStr, pos, 1.0F, MakeVector4(0.5, 0.5, 0.5, 1));
		}

		void Client::DrawStats() {
			SPADES_MARK_FUNCTION();

			// only draw stats when scoreboard is visible
			const int statsMode = cg_stats;
			if (statsMode >= 3 && !scoreboardVisible)
				return;

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			char buf[64];
			std::string str;

			{
				auto fps = fpsCounter.GetFps();
				if (fps <= 0) {
					str += "FPS: NA";
				} else {
					snprintf(buf, sizeof(buf), "%.0fFPS", fps);
					str += buf;
				}
			}
			{
				// Display world updates per second
				auto ups = upsCounter.GetFps();
				if (ups <= 0) {
					str += ", UPS: NA";
				} else {
					snprintf(buf, sizeof(buf), ", %.0fUPS", ups);
					str += buf;
				}
			}

			if (!IsDemoMode()) {
				auto ping = activeNet->GetPing();
				snprintf(buf, sizeof(buf), ", ping: %dms", ping);
				str += buf;

				auto upbps = activeNet->GetUplinkBps() / 1000;
				auto downbps = activeNet->GetDownlinkBps() / 1000;
				snprintf(buf, sizeof(buf), ", up/down: %.02f/%.02fkbps", upbps, downbps);
				str += buf;

				auto loss = activeNet->GetPacketLoss() * 100.0F;
				snprintf(buf, sizeof(buf), ", loss: %.0f%%", loss);
				str += buf;

				auto throttle = activeNet->GetPacketThrottle();
				auto choke = (1.0F - throttle) * 100.0F;
				snprintf(buf, sizeof(buf), ", choke: %.0f%%", choke);
				str += buf;
			}

			// add margin
			const float margin = 2.0F;
			IFont& font = cg_statsSmallFont
				? fontManager->GetSmallFont()
				: fontManager->GetGuiFont();
			Vector2 size = font.Measure(str) + (margin * 2.0F);
			Vector2 pos = MakeVector2(sw, sh) - size;
			pos *= MakeVector2(0.5F, (statsMode < 2) ? 1.0F : 0.0F);

			Vector4 color = MakeVector4(1, 1, 1, 1);
			Vector4 outline = MakeVector4(0, 0, 0, 0.8F);

			// draw text
			pos += MakeVector2(margin, margin);
			font.DrawOutline(str, pos, 1.0F, color, outline);
		}

		void Client::Draw2D() {
			SPADES_MARK_FUNCTION();

			if (GetWorld())
				Draw2DWithWorld();
			else
				Draw2DWithoutWorld();
		}
	} // namespace client
} // namespace spades