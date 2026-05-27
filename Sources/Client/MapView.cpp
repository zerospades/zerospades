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

#include <algorithm>
#include <utility>

#include "CTFGameMode.h"
#include "Client.h"
#include "Fonts.h"
#include "GameMap.h"
#include "IImage.h"
#include "IRenderer.h"
#include "MapView.h"
#include "Player.h"
#include "TCGameMode.h"
#include "Weapon.h"
#include "World.h"
#include <Core/Settings.h>
#include <Core/TMPUtils.h>

DEFINE_SPADES_SETTING(cg_minimapOpacity, "1");
DEFINE_SPADES_SETTING(cg_minimapSize, "128");
DEFINE_SPADES_SETTING(cg_minimapScaleMode, "2");
DEFINE_SPADES_SETTING(cg_minimapCoords, "1");
DEFINE_SPADES_SETTING(cg_minimapPlayerIcon, "1");
DEFINE_SPADES_SETTING(cg_minimapPlayerColor, "1");
DEFINE_SPADES_SETTING(cg_minimapPlayerNames, "0");

SPADES_SETTING(cg_stats);
SPADES_SETTING(cg_statsSmallFont);

using std::pair;
using stmp::optional;

namespace spades {
	namespace client {
		namespace {
			optional<pair<Vector2, Vector2>> ClipLineSegment(
				const pair<Vector2, Vector2>& inLine, const Plane2& plane) {
				const float d1 = plane.GetDistanceTo(inLine.first);
				const float d2 = plane.GetDistanceTo(inLine.second);
				int bits = (d1 > 0 ? 1 : 0) | (d2 > 0 ? 2 : 0);
				switch (bits) {
					case 0: return {};
					case 3: return inLine;
				}

				const float fraction = d1 / (d1 - d2);
				Vector2 intersection = Mix(inLine.first, inLine.second, fraction);
				if (bits == 1)
					return std::make_pair(inLine.first, intersection);
				else
					return std::make_pair(intersection, inLine.second);
			}

			optional<pair<Vector2, Vector2>> ClipLineSegment(const pair<Vector2,
				Vector2>& inLine, const AABB2& rect) {
				auto line = ClipLineSegment(inLine, Plane2{1, 0, -rect.GetMinX()});
				if (!line)
					return line;
				line = ClipLineSegment(*line, Plane2{-1, 0, rect.GetMaxX()});
				if (!line)
					return line;
				line = ClipLineSegment(*line, Plane2{0, 1, -rect.GetMinY()});
				if (!line)
					return line;
				line = ClipLineSegment(*line, Plane2{0, -1, rect.GetMaxY()});
				return line;
			}
		} // namespace

		MapView::MapView(Client* c, bool largeMap)
		    : client(c), renderer(c->GetRenderer()), largeMap(largeMap) {
			actualScale = 1.0F;
			lastScale = 1.0F;
			zoomed = false;
			zoomState = 0.0F;
		}

		MapView::~MapView() {}

		void MapView::Update(float dt) {
			if (largeMap) {
				if (zoomed) {
					zoomState += dt * 5.0F;
					if (zoomState > 1.0F)
						zoomState = 1.0F;
				} else {
					zoomState -= dt * 5.0F;
					if (zoomState < 0.0F)
						zoomState = 0.0F;
				}
				return;
			}

			int mode = cg_minimapScaleMode;
			if (scaleMode != mode) {
				lastScale = actualScale;
				scaleMode = mode;
			}

			float scale = 0.0F;
			switch (scaleMode) {
				case 0: scale = 1.0F / 4.0F; break; // 400%
				case 1: scale = 1.0F / 2.0F; break; // 200%
				case 2: scale = 1.0F; break;        // 100%
				case 3: scale = 2.0F; break;        // 50%
				default: SPAssert(false);
			}

			if (actualScale != scale) {
				float spd = fabsf(scale - lastScale) * 10.0F;
				spd = std::max(spd, 0.2F);
				spd *= dt;
				if (scale > actualScale) {
					actualScale += spd;
					if (actualScale > scale)
						actualScale = scale;
				} else {
					actualScale -= spd;
					if (actualScale < scale)
						actualScale = scale;
				}
			}
		}

		Vector2 MapView::Project(const Vector2& pos) const {
			Vector2 scrPos;
			scrPos.x = (pos.x - inRect.GetMinX()) / inRect.GetWidth();
			scrPos.x = (scrPos.x * outRect.GetWidth()) + outRect.GetMinX();
			scrPos.y = (pos.y - inRect.GetMinY()) / inRect.GetHeight();
			scrPos.y = (scrPos.y * outRect.GetHeight()) + outRect.GetMinY();
			return scrPos;
		}

		void MapView::DrawIcon(Vector3 pos, IImage& img, const Vector4& col, float rotation) {
			if ((int)cg_minimapPlayerIcon >= 2 && rotation == 0.0F) {
				pos.x = Clamp(pos.x, inRect.GetMinX(), inRect.GetMaxX());
				pos.y = Clamp(pos.y, inRect.GetMinY(), inRect.GetMaxY());
			} else {
				if (pos.x < inRect.GetMinX() || pos.x > inRect.GetMaxX() ||
				    pos.y < inRect.GetMinY() || pos.y > inRect.GetMaxY())
					return;
			}

			Vector2 scrPos = Project(Vector2{pos.x, pos.y});

			// rounded for better pixel alignment
			if (rotation == 0.0F) {
				scrPos.x = roundf(scrPos.x);
				scrPos.y = roundf(scrPos.y);
			}

			float c = (rotation != 0.0F) ? cosf(rotation) : 1.0F;
			float s = (rotation != 0.0F) ? sinf(rotation) : 0.0F;
			static const float coords[][2] = {{-1, -1}, {1, -1}, {-1, 1}};
			const AABB2 inRect{0.0F, 0.0F, img.GetWidth(), img.GetHeight()};

			Vector2 u = MakeVector2(inRect.GetMaxX() * 0.5F, 0.0F);
			Vector2 v = MakeVector2(0.0F, inRect.GetMaxY() * 0.5F);

			Vector2 vt[3];
			for (int i = 0; i < 3; i++) {
				Vector2 ss = u * coords[i][0] + v * coords[i][1];
				vt[i].x = scrPos.x + ss.x * c - ss.y * s;
				vt[i].y = scrPos.y + ss.x * s + ss.y * c;
			}

			renderer.SetColorAlphaPremultiplied(col);
			renderer.DrawImage(img, vt[0], vt[1], vt[2], inRect);
		}

		void MapView::DrawText(IFont& font, std::string s, Vector3 pos, const Vector4& col) {
			if (pos.x < inRect.GetMinX() || pos.x > inRect.GetMaxX() ||
				pos.y < inRect.GetMinY() || pos.y > inRect.GetMaxY())
				return;

			Vector2 scrPos = Project(Vector2{pos.x, pos.y});
			Vector2 size = font.Measure(s);
			scrPos.x -= size.x * 0.5F;
			scrPos.y -= size.y;

			// rounded for better pixel alignment
			scrPos.x = floorf(scrPos.x);
			scrPos.y = floorf(scrPos.y);

			font.DrawShadow(s, scrPos, 1.0F, col, MakeVector4(0, 0, 0, col.w));
		}

		void MapView::SwitchScale() {
			scaleMode = (scaleMode + 1) % 4;
			lastScale = actualScale;
			cg_minimapScaleMode = scaleMode;
		}

		std::string MapView::ToGrid(float x, float y) {
			auto letter = char(int('A') + int(x / 64));
			auto number = std::to_string(int(y / 64) + 1);
			return letter + number;
		}

		// definite a palette of 32 color in RGB code
		int palette[32][3] = {
		  {0, 0, 0},       // 0  Black			#000000
		  {255, 255, 255}, // 1  White			#FFFFFF
		  {128, 128, 128}, // 2  Dark Grey		#808080
		  {255, 255, 0},   // 3  Yellow			#FFFF00
		  {0, 255, 255},   // 4  Cyan			#00FFFF
		  {255, 0, 255},   // 5  Magenta		#FF00FF
		  {255, 0, 0},     // 6  Red			#FF0000
		  {0, 255, 0},     // 7  Bright Green	#00FF00
		  {0, 0, 255},     // 8  Blue			#0000FF
		  {128, 0, 0},     // 9  Dark Red		#800000
		  {0, 128, 0},     // 10 Green			#008000
		  {0, 0, 128},     // 11 Navy Blue		#000080
		  {128, 128, 0},   // 12 Olive			#808000
		  {128, 0, 128},   // 13 Purple			#800080
		  {0, 128, 128},   // 14 Teal			#008080
		  {255, 128, 0},   // 15 Orange			#FF8000
		  {255, 0, 128},   // 16 Pink			#FF0080
		  {128, 0, 255},   // 17 Violet			#8000FF
		  {0, 128, 255},   // 18 Bluette		#0080FF
		  {128, 255, 0},   // 19 Lime Green		#80FF00
		  {0, 255, 128},   // 20 Spring Green	#00FF80
		  {255, 128, 128}, // 21 Salmon			#FF8080
		  {128, 255, 128}, // 22 Light Green	#80FF80
		  {128, 128, 255}, // 23 Light Blue		#8080FF
		  {128, 255, 255}, // 24 Light Cyan		#80FFFF
		  {255, 255, 128}, // 25 Light Yellow	#FFFF80
		  {255, 128, 255}, // 26 Light Magenta	#FF80FF
		  {165, 42, 42},   // 27 Maroon			#A52A2A
		  {255, 69, 0},    // 28 Scarlet		#FF4500
		  {255, 165, 0},   // 29 Orange			#FFA500
		  {139, 69, 19},   // 30 Brown			#8B4513
		  {210, 105, 30},  // 31 Chocolate		#D2691E
		};

		void MapView::Draw() {
			World* world = client->GetWorld();
			if (!world)
				return;

			auto cameraMode = client->GetCameraMode();

			bool isFollowing = HasTargetPlayer(cameraMode);
			bool isFollowingNonLocal = FollowsNonLocalPlayer(cameraMode);
			bool isFreeCamera = cameraMode == ClientCameraMode::Free;

			// The player to focus on
			stmp::optional<Player&> focusPlayerPtr;
			Vector3 focusPlayerPos;
			float focusPlayerAngle;
	
			if (isFollowing) {
				int targetId = client->GetCameraTargetPlayerId();
				auto maybeTarget = world->GetPlayer(targetId);
				if (!maybeTarget)
					return;
				Player& player = maybeTarget.value();
				Vector3 o = player.GetFront2D();

				focusPlayerPos = player.GetPosition();
				focusPlayerAngle = atan2f(o.y, o.x) + M_PI_F * 0.5F;
				focusPlayerPtr = player;
			} else if (isFreeCamera) {
				focusPlayerPos = client->freeCameraState.position;
				focusPlayerAngle = client->followAndFreeCameraState.yaw - M_PI_F * 0.5F;
				focusPlayerPtr = world->GetLocalPlayer(); // May be empty in demo mode
			} else {
				return;
			}

			// The local player (this is important for access control)
			// In demo mode, there's no local player - treat as spectator
			stmp::optional<Player&> maybePlayer = world->GetLocalPlayer();
			bool localPlayerIsSpectator = client->IsDemoMode() ||
				(maybePlayer && maybePlayer->IsSpectator());
			bool localPlayerIsSpectating = localPlayerIsSpectator || client->staffSpectating;

			// Need either a local player or demo mode to continue
			if (!maybePlayer && !client->IsDemoMode())
				return;

			// Pointers for safe access (may be null in demo mode)
			Player* localPlayer = maybePlayer ? &maybePlayer.value() : nullptr;
			Player* focusPlayer = focusPlayerPtr ? &focusPlayerPtr.value() : nullptr;

			if (largeMap && zoomState < 0.0001F)
				return;

			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();

			Handle<GameMap> map = world->GetMap();
			SPAssert(map);

			Vector2 mapSize = MakeVector2((float)map->Width(), (float)map->Height());

			float cfgMapSize = Clamp((float)cg_minimapSize, 32.0F, 256.0F);
			Vector2 mapWndSize = {cfgMapSize, cfgMapSize};

			Vector2 zoomedSize = {512.0F, 512.0F};
			if (sw < zoomedSize.x || sh < zoomedSize.y)
				zoomedSize *= 0.75F;

			Vector2 center = {focusPlayerPos.x, focusPlayerPos.y};
			center = Mix(center, mapSize * 0.5F, zoomState);

			if (largeMap) {
				float per = zoomState;
				per = 1.0F - per;
				per *= per;
				per = 1.0F - per;
				per = Mix(0.0F, 1.0F, per);
				zoomedSize = Mix(mapWndSize, zoomedSize, per);
				mapWndSize = zoomedSize;
			}

			Vector2 inRange = mapWndSize * 0.5F * actualScale;
			AABB2 inRect(center - inRange, center + inRange);
			if (largeMap) {
				inRect.min = MakeVector2(0, 0);
				inRect.max = mapSize;
			} else {
				if (inRect.GetMinX() < 0.0F)
					inRect = inRect.Translated(-inRect.GetMinX(), 0);
				if (inRect.GetMinY() < 0.0F)
					inRect = inRect.Translated(0, -inRect.GetMinY());
				if (inRect.GetMaxX() > mapSize.x)
					inRect = inRect.Translated(mapSize.x - inRect.GetMaxX(), 0);
				if (inRect.GetMaxY() > mapSize.y)
					inRect = inRect.Translated(0, mapSize.y - inRect.GetMaxY());
			}

			float winX = (sw - 8.0F) - mapWndSize.x;
			float winY = 8.0F;

			if (!largeMap) {
				const int statsMode = cg_stats;
				if (statsMode == 2 || (statsMode >= 3 && client->IsScoreboardVisible()))
					winY += cg_statsSmallFont ? 10.0F : 20.0F;
			}

			AABB2 outRect(winX, winY, mapWndSize.x, mapWndSize.y);
			if (largeMap) {
				outRect.min = MakeVector2(sw - zoomedSize.x, sh - zoomedSize.y) * 0.5F;
				outRect.max = MakeVector2(sw + zoomedSize.x, sh + zoomedSize.y) * 0.5F;
			}

			this->inRect = inRect;
			this->outRect = outRect;

			float largeMapAlpha = largeMap ? zoomState : 1.0F;
			float alpha = largeMap ? largeMapAlpha : Clamp((float)cg_minimapOpacity, 0.1F, 1.0F);

			// draw map
			renderer.SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1) * alpha);
			renderer.DrawFlatGameMap(outRect, inRect);

			// draw map border
			renderer.SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 1) * alpha);
			renderer.DrawOutlinedRect(outRect.GetMinX() - 1, outRect.GetMinY() - 1,
			                          outRect.GetMaxX() + 1, outRect.GetMaxY() + 1);

			// draw grid lines
			Vector2 gridSize = mapSize / 8.0F;
			Vector4 gridCol = MakeVector4(0, 0, 0, 1) * 0.8F * alpha;
			renderer.SetColorAlphaPremultiplied(gridCol);
			for (float x = gridSize.x; x < inRect.GetMaxX() - 1; x += gridSize.x) {
				float wx = (x - inRect.GetMinX()) / inRect.GetWidth();
				if (wx < 0.0F || wx >= 1.0F)
					continue;
				wx = (wx * outRect.GetWidth()) + outRect.GetMinX();
				wx = roundf(wx); // rounded for better pixel alignment
				for (float dx = 0; dx < outRect.GetHeight(); dx += 4) {
					renderer.DrawImage(nullptr, MakeVector2(wx, outRect.GetMinY() + dx),
					                   AABB2(0, 0, 1, 2));
				}
			}
			for (float y = gridSize.y; y < inRect.GetMaxY() - 1; y += gridSize.y) {
				float wy = (y - inRect.GetMinY()) / inRect.GetHeight();
				if (wy < 0.0F || wy >= 1.0F)
					continue;
				wy = (wy * outRect.GetHeight()) + outRect.GetMinY();
				wy = roundf(wy); // rounded for better pixel alignment
				for (float dy = 0; dy < outRect.GetWidth(); dy += 4) {
					renderer.DrawImage(nullptr, MakeVector2(outRect.GetMinX() + dy, wy),
					                   AABB2(0, 0, 2, 1));
				}
			}

			// draw grid label
			Handle<IImage> mapFont = renderer.RegisterImage("Gfx/Fonts/MapFont.tga");
			Vector4 labelCol = MakeVector4(1, 1, 1, 1) * 0.8F * alpha;
			for (int i = 0; i < 8; i++) {
				float startX = (float)i * gridSize.x;
				float endX = startX + gridSize.x;

				if (startX > inRect.GetMaxX() || endX < inRect.GetMinX())
					continue;

				float fade = std::min((std::min(endX, inRect.GetMaxX())
					  - std::max(startX, inRect.GetMinX()))
					  / (endX - startX) * 2.0F, 1.0F);

				renderer.SetColorAlphaPremultiplied(labelCol * fade);

				float center = std::max(startX, inRect.GetMinX());
				center = 0.5F * (center + std::min(endX, inRect.GetMaxX()));

				float wx = (center - inRect.GetMinX()) / inRect.GetWidth();
				wx = (wx * outRect.GetWidth()) + outRect.GetMinX();
				wx = roundf(wx); // rounded for better pixel alignment

				float fntX = static_cast<float>((i & 3) * 8);
				float fntY = static_cast<float>((i >> 2) * 8);

				renderer.DrawImage(mapFont, MakeVector2(wx - 4, outRect.GetMinY() + 4),
				                   AABB2(fntX, fntY, 8, 8));
			}
			for (int i = 0; i < 8; i++) {
				float startY = (float)i * gridSize.y;
				float endY = startY + gridSize.y;

				if (startY > inRect.GetMaxY() || endY < inRect.GetMinY())
					continue;

				float fade = std::min((std::min(endY, inRect.GetMaxY())
					- std::max(startY, inRect.GetMinY()))
					/ (endY - startY) * 2.0F, 1.0F);

				renderer.SetColorAlphaPremultiplied(labelCol * fade);

				float center = std::max(startY, inRect.GetMinY());
				center = 0.5F * (center + std::min(endY, inRect.GetMaxY()));

				float wy = (center - inRect.GetMinY()) / inRect.GetHeight();
				wy = (wy * outRect.GetHeight()) + outRect.GetMinY();
				wy = roundf(wy); // rounded for better pixel alignment

				float fntX = static_cast<float>((i & 3) * 8);
				float fntY = static_cast<float>((i >> 2) * 8 + 16);

				renderer.DrawImage(mapFont, MakeVector2(outRect.GetMinX() + 4, wy - 4),
				                   AABB2(fntX, fntY, 8, 8));
			}

			// draw bullet tracers
			Handle<IImage> tracerImg = renderer.RegisterImage("Gfx/White.tga");
			const float tracerW = 0.5F;
			const AABB2 tracerInRect{0.0F, 0.0F, tracerImg->GetWidth(), tracerImg->GetHeight()};

			renderer.SetColorAlphaPremultiplied(MakeVector4(1, 1, 0, 1) * largeMapAlpha);
			for (const auto& localEntity : client->localEntities) {
				auto* const tracer = dynamic_cast<MapViewTracer*>(localEntity.get());
				if (!tracer)
					continue;

				const auto line1 = tracer->GetLineSegment();
				if (!line1)
					continue;

				auto line2 = ClipLineSegment(std::make_pair(Vector2{(*line1).first.x,
					(*line1).first.y}, Vector2{(*line1).second.x, (*line1).second.y}), inRect);
				if (!line2)
					continue;

				auto& line3 = *line2;
				line3.first = Project(line3.first);
				line3.second = Project(line3.second);

				if (line3.first == line3.second)
					continue;

				Vector2 normal = (line3.second - line3.first).Normalize();
				normal = {-normal.y, normal.x};

				{
					const Vector2 vt[] = {
						line3.first - normal * tracerW,
						line3.first + normal * tracerW,
						line3.second - normal * tracerW
					};

					renderer.DrawImage(tracerImg, vt[0], vt[1], vt[2], tracerInRect);
				}
			}

			// draw player's icon
			const int iconMode = cg_minimapPlayerIcon;
			const int colorMode = cg_minimapPlayerColor;
			const int namesMode = cg_minimapPlayerNames;

			const auto& spectatorColor = MakeIntVector3(200, 200, 200);
			const auto& localPlayerColor = MakeIntVector3(0, 255, 255);

			Handle<IImage> playerIcon = renderer.RegisterImage("Gfx/Map/Player.png");
			Handle<IImage> playerRifleIcon = renderer.RegisterImage("Gfx/Map/Rifle.png");
			Handle<IImage> playerSMGIcon = renderer.RegisterImage("Gfx/Map/SMG.png");
			Handle<IImage> playerShotgunIcon = renderer.RegisterImage("Gfx/Map/Shotgun.png");
			Handle<IImage> playerViewIcon = renderer.RegisterImage("Gfx/Map/View.png");
			Handle<IImage> playerADSViewIcon = renderer.RegisterImage("Gfx/Map/ViewADS.png");

			IFont& smallFont = client->fontManager->GetSmallFont();

			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue; // player is non-existent

				Player& p = maybePlayer.value();
				if (p.IsSpectator() || !p.IsAlive())
					continue; // don't draw dead players or spectators
				if (!localPlayerIsSpectating && localPlayer && !localPlayer->IsTeammate(p))
					continue; // don't draw enemies when not spectating a player

				// dont draw the focused player icon if we are NOT on staff spectating mode
				if (focusPlayer && &p == focusPlayer && !client->staffSpectating)
					continue;

				IntVector3 iconColor = world->GetTeamColor(p.GetTeamId());
				if (localPlayer && &p == localPlayer) {
					iconColor = localPlayerColor;
				} else if (colorMode) {
					int colorIndex = i % 32;
					iconColor = MakeIntVector3(
						palette[colorIndex][0],
						palette[colorIndex][1],
						palette[colorIndex][2]
					);
				}
				Vector4 iconColorF = ModifyColor(iconColor) * largeMapAlpha;


				Handle<IImage> iconImg = playerIcon;
				if (iconMode) {
					switch (p.GetWeaponType()) {
						case RIFLE_WEAPON: iconImg = playerRifleIcon; break;
						case SMG_WEAPON: iconImg = playerSMGIcon; break;
						case SHOTGUN_WEAPON: iconImg = playerShotgunIcon; break;
					}
				}

				// draw player icons
				const auto& pos = p.GetPosition();
				const auto& o = p.GetFront2D();
				float playerAngle = atan2f(o.y, o.x) + M_PI_F * 0.5F;
				DrawIcon(pos, *iconImg, iconColorF, playerAngle);

				// dont draw the focused player name when following non-local players
				if (focusPlayer && &p == focusPlayer && isFollowingNonLocal)
					continue;

				// draw player names
				if (namesMode == 1 || (namesMode >= 2 && largeMap))
					DrawText(smallFont, p.GetName(), pos,
					         MakeVector4(1, 1, 1, 0.75F * largeMapAlpha));
			}

			// draw the focused player view
			if (focusPlayer && (focusPlayer->IsAlive() || localPlayerIsSpectating)) {
				IntVector3 iconColor = world->GetTeamColor(focusPlayer->GetTeamId());
				if (focusPlayer->IsLocalPlayer()) {
					iconColor = localPlayerIsSpectating
						? spectatorColor : localPlayerColor;
				} else if (colorMode) {
					int colorIndex = focusPlayer->GetId() % 32;
					iconColor = MakeIntVector3(
						palette[colorIndex][0],
						palette[colorIndex][1],
						palette[colorIndex][2]
					);
				}
				Vector4 iconColorF = ModifyColor(iconColor) * largeMapAlpha;

				Handle<IImage> iconImg = playerIcon;
				if (iconMode && !isFreeCamera) {
					switch (focusPlayer->GetWeaponType()) {
						case RIFLE_WEAPON: iconImg = playerRifleIcon; break;
						case SMG_WEAPON: iconImg = playerSMGIcon; break;
						case SHOTGUN_WEAPON: iconImg = playerShotgunIcon; break;
					}
				}

				float zoomState = localPlayerIsSpectating
					? client->spectatorZoomState : client->GetAimDownState();
				DrawIcon(focusPlayerPos, (zoomState > 0.99F)
					? *playerADSViewIcon : *playerViewIcon,
					iconColorF * 0.9F, focusPlayerAngle);
				DrawIcon(focusPlayerPos, *iconImg, iconColorF, focusPlayerAngle);
			} else if (localPlayerIsSpectating && isFreeCamera) {
				// In demo free camera mode, draw a simple view indicator
				IntVector3 iconColor = spectatorColor;
				Vector4 iconColorF = ModifyColor(iconColor) * largeMapAlpha;
				float zoomState = client->spectatorZoomState;
				DrawIcon(focusPlayerPos, (zoomState > 0.99F)
					? *playerADSViewIcon : *playerViewIcon,
					iconColorF * 0.9F, focusPlayerAngle);
			}

			// draw map objects
			Handle<IImage> baseIcon = renderer.RegisterImage("Gfx/Map/CommandPost.png");
			Handle<IImage> intelIcon = renderer.RegisterImage("Gfx/Map/Intel.png");
			stmp::optional<IGameMode&> mode = world->GetMode();
			if (mode && mode->ModeType() == IGameMode::m_CTF) {
				auto& ctf = dynamic_cast<CTFGameMode&>(mode.value());
				for (int tId = 0; tId < 2; tId++) {
					CTFGameMode::Team& team1 = ctf.GetTeam(tId);
					CTFGameMode::Team& team2 = ctf.GetTeam(1 - tId);

					// draw base
					Vector4 teamColorF = ModifyColor(world->GetTeamColor(tId)) * largeMapAlpha;
					DrawIcon(team1.basePos, *baseIcon, teamColorF);

					// draw both flags
					if (team2.hasIntel) {
						stmp::optional<Player&> carrier = world->GetPlayer(team2.carrierId);
						if (carrier && (localPlayerIsSpectating || (localPlayer && carrier->IsTeammate(*localPlayer)))) {
							float pulse = std::max(0.5F, fabsf(sinf(world->GetTime() * 4.0F)));
							DrawIcon(carrier->GetPosition(), *intelIcon, teamColorF * pulse);
						}
					} else {
						DrawIcon(team1.flagPos, *intelIcon, teamColorF);
					}
				}
			} else if (mode && mode->ModeType() == IGameMode::m_TC) {
				auto& tc = dynamic_cast<TCGameMode&>(mode.value());
				for (int i = 0; i < tc.GetNumTerritories(); i++) {
					TCGameMode::Territory& t = tc.GetTerritory(i);
					IntVector3 teamColor = (t.ownerTeamId >= NEUTRAL_TEAM)
					                         ? MakeIntVector3(128, 128, 128)
					                         : world->GetTeamColor(t.ownerTeamId);

					Vector4 teamColorF = ModifyColor(teamColor) * largeMapAlpha;
					DrawIcon(t.pos, *baseIcon, teamColorF);
				}
			}

			// draw map sector
			if (!largeMap && cg_minimapCoords) {
				IFont& font = client->fontManager->GetGuiFont();
				auto gridStr = ToGrid(focusPlayerPos.x, focusPlayerPos.y);
				Vector2 size = font.Measure(gridStr);
				Vector2 pos = outRect.min;
				if ((int)cg_minimapCoords < 2) {
					pos.x += ((outRect.GetWidth() - size.x) * 0.5F);
					pos.y = outRect.GetMaxY() + size.y * 0.5F - 8.0F;
				} else {
					pos.x = (pos.x - 8.0F) - size.x + 2.0F;
					pos.y += ((outRect.GetHeight() - size.y) * 0.5F);
				}

				Vector4 color = focusPlayer
					? ConvertColorRGBA(focusPlayer->GetColor())
					: MakeVector4(1, 1, 1, 1);
				float luminosity = color.x + color.y + color.z;
				Vector4 shadowColor = (luminosity > 0.9F)
					? MakeVector4(0, 0, 0, 0.8F)
					: MakeVector4(1, 1, 1, 0.8F);

				color.w *= largeMapAlpha;
				shadowColor.w *= largeMapAlpha;

				font.DrawShadow(gridStr, pos, 1.0F, color, shadowColor);
			}
		}

		MapViewTracer::MapViewTracer(Vector3 p1, Vector3 p2)
		    : startPos(p1), velocity(300.0F) {
			// Z coordinate doesn't matter in MapView
			p1.z = p2.z = 0.0F;

			dir = (p2 - p1).Normalize();
			length = (p2 - p1).GetLength();

			const float maxTimeSpread = 1.0F / 20.0F;
			const float shutterTime = 1.0F / 20.0F;

			visibleLength = shutterTime * velocity;
			curDistance = -visibleLength;

			// Randomize the starting position within the range of the shutter
			// time. However, make sure the tracer is displayed for at least one frame.
			curDistance += std::min(length + visibleLength,
				maxTimeSpread * SampleRandomFloat() * velocity);

			firstUpdate = true;
		}

		bool MapViewTracer::Update(float dt) {
			if (!firstUpdate) {
				curDistance += dt * velocity;
				if (curDistance > length)
					return false;
			}

			firstUpdate = false;
			return true;
		}

		stmp::optional<std::pair<Vector3, Vector3>> MapViewTracer::GetLineSegment() {
			float startDist = curDistance;
			float endDist = curDistance + visibleLength;
			startDist = std::max(startDist, 0.0F);
			endDist = std::min(endDist, length);
			if (startDist >= endDist)
				return {};

			Vector3 pos1 = startPos + dir * startDist;
			Vector3 pos2 = startPos + dir * endDist;
			return std::make_pair(pos1, pos2);
		}

		MapViewTracer::~MapViewTracer() {}
	} // namespace client
} // namespace spades