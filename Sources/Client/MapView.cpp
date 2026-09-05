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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <algorithm>
#include <utility>

#include "CTFGameMode.h"
#include "Client.h"
#include "ExtendedTeamplay.h"
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
DEFINE_SPADES_SETTING(cg_minimapCircular, "0");
DEFINE_SPADES_SETTING(cg_minimapRotating, "0");
DEFINE_SPADES_SETTING(cg_minimapSize, "128");
DEFINE_SPADES_SETTING(cg_minimapScaleMode, "2");
DEFINE_SPADES_SETTING(cg_minimapCoords, "1");
DEFINE_SPADES_SETTING(cg_minimapPlayerIcon, "1");
DEFINE_SPADES_SETTING(cg_minimapPlayerColor, "1");
DEFINE_SPADES_SETTING(cg_minimapPlayerNames, "0");
DEFINE_SPADES_SETTING(cg_minimapPlayerSounds, "0");

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

			optional<pair<Vector2, Vector2>> ClipLineSegment(
				const pair<Vector2, Vector2>& line, const Vector2& center, float radius) {

				Vector2 d = line.second - line.first;
				Vector2 f = line.first - center;

				float a = Vector2::Dot(d, d);
				float b = 2.0F * Vector2::Dot(f, d);
				float c = Vector2::Dot(f, f) - radius * radius;
				float disc = b*b - 4.0F*a*c;

				if (disc < 0.0F)
					return {}; // no intersection

				disc = sqrtf(disc);
				float t0 = (-b - disc) / (2.0F*a);
				float t1 = (-b + disc) / (2.0F*a);

				// clamp to [0, 1]
				t0 = std::max(t0, 0.0F);
				t1 = std::min(t1, 1.0F);

				if (t0 > t1)
					return {};

				return std::make_pair(
					line.first + d * t0,
					line.first + d * t1
				);
			}
		} // namespace

		MapView::MapView(Client* c, bool largeMap)
			: client(c), renderer(c->GetRenderer()), largeMap(largeMap) {
			actualScale = 1.0F;
			lastScale = 1.0F;
			zoomed = false;
			zoomState = 0.0F;
			circularMap = false;
			rotatingMap = false;
			mapAngle = 0.0F;
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
				case 2: scale = 1.0F; break;		// 100%
				case 3: scale = 2.0F; break;		// 50%
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

		inline Vector2 Rotate(const Vector2& v, const Vector2& a) {
			return MakeVector2(v.x*a.x - v.y*a.y, v.x*a.y + v.y*a.x);
		}

		Vector2 MapView::RotateMap(const Vector2& offset) const {
			return scrCenter + Rotate(offset, mapRotation);
		}

		Vector2 MapView::Project(const Vector2& mapPos, bool rotated) const {
			if (rotated) {
				Vector2 rel = mapPos - mapCenter;
				Vector2 rot = Rotate(rel, mapRotation);
				float scaleX = outRect.GetWidth() / inRect.GetWidth();
				float scaleY = outRect.GetHeight() / inRect.GetHeight();
				return scrCenter + MakeVector2(rot.x*scaleX, rot.y*scaleY);
			}

			Vector2 scrPos;
			scrPos.x = (mapPos.x - inRect.GetMinX()) / inRect.GetWidth();
			scrPos.y = (mapPos.y - inRect.GetMinY()) / inRect.GetHeight();
			scrPos.x = (scrPos.x * outRect.GetWidth()) + outRect.GetMinX();
			scrPos.y = (scrPos.y * outRect.GetHeight()) + outRect.GetMinY();
			return scrPos;
		}

		void MapView::DrawGridLines(const Vector2& gridSize, const AABB2& gridRect, const Vector4& col, bool rotates) {
			renderer.SetColorAlphaPremultiplied(col);
			for (float x = gridSize.x; x < gridRect.GetMaxX() - 1; x += gridSize.x) {
				if (x < gridRect.GetMinX() || x >= gridRect.GetMaxX())
					continue;
				const auto& p1 = Project(MakeVector2(x, gridRect.GetMinY()), rotates);
				if (!rotates) {
					float wx = roundf(p1.x); // rounded for better pixel alignment
					for (float dx = 0; dx < outRect.GetHeight(); dx += 4) {
						renderer.DrawImage(nullptr,
							MakeVector2(wx - 0.5F, outRect.GetMinY() + dx),
								AABB2(0, 0, 1, 2));
					}
				} else {
					const auto& p2 = Project(MakeVector2(x, gridRect.GetMaxY()), rotates);
					renderer.DrawLine(p1, p2);
				}
			}
			for (float y = gridSize.y; y < gridRect.GetMaxY() - 1; y += gridSize.y) {
				if (y < gridRect.GetMinY() || y >= gridRect.GetMaxY())
					continue;
				const auto& p1 = Project(MakeVector2(gridRect.GetMinX(), y), rotates);
				if (!rotates) {
					float wy = roundf(p1.y); // rounded for better pixel alignment
					for (float dy = 0; dy < outRect.GetWidth(); dy += 4) {
						renderer.DrawImage(nullptr,
							MakeVector2(outRect.GetMinX() + dy, wy - 0.5F),
								AABB2(0, 0, 2, 1));
					}
				} else {
					const auto& p2 = Project(MakeVector2(gridRect.GetMaxX(), y), rotates);
					renderer.DrawLine(p1, p2);
				}
			}
		}

		void MapView::DrawIcon(const Vector2& pos, IImage& img, const Vector4& col, float rotation) {
			bool rotates = rotation != 0.0F;
			Vector2 scrPos;
			if (circularMap) {
				scrPos = Project(pos, true);
				Vector2 rel = scrPos - scrCenter;
				float len = rel.GetLength();
				bool outside = len > scrRadius;
				if ((int)cg_minimapPlayerIcon >= 2 && !rotates) { // clamp to edge
					if (outside && len > 0.0001F)
						scrPos = scrCenter + rel * (scrRadius / len);
				} else if (outside) {
					return;
				}
				if (rotates && mapAngle != 0.0F)
					rotation += mapAngle;
			} else if (rotatingMap) {
				scrPos = Project(pos, true);
				if ((int)cg_minimapPlayerIcon >= 2 && !rotates) { // clamp to edge
					scrPos.x = Clamp(scrPos.x, outRect.GetMinX(), outRect.GetMaxX());
					scrPos.y = Clamp(scrPos.y, outRect.GetMinY(), outRect.GetMaxY());
				} else if (!outRect.Contains(scrPos)) {
					return;
				}
				if (rotates && mapAngle != 0.0F)
					rotation += mapAngle;
			} else {
				scrPos = pos;
				if ((int)cg_minimapPlayerIcon >= 2 && !rotates) { // clamp to edge
					scrPos.x = Clamp(scrPos.x, inRect.GetMinX(), inRect.GetMaxX());
					scrPos.y = Clamp(scrPos.y, inRect.GetMinY(), inRect.GetMaxY());
				} else if (!inRect.Contains(scrPos)) {
					return;
				}
				scrPos = Project(scrPos);
			}

			// rounded for better pixel alignment
			if (!rotates) {
				scrPos.x = roundf(scrPos.x);
				scrPos.y = roundf(scrPos.y);
			}

			const float c = rotates ? cosf(rotation) : 1.0F;
			const float s = rotates ? sinf(rotation) : 0.0F;
			static const float coords[][2] = {{-1, -1}, {1, -1}, {-1, 1}};

			const AABB2 inRect{0.0F, 0.0F, img.GetWidth(), img.GetHeight()};
			const auto& u = MakeVector2(inRect.GetMaxX() * 0.5F, 0.0F);
			const auto& v = MakeVector2(0.0F, inRect.GetMaxY() * 0.5F);

			Vector2 vt[3];
			for (int i = 0; i < 3; i++) {
				const auto& ss = u * coords[i][0] + v * coords[i][1];
				vt[i].x = scrPos.x + ss.x * c - ss.y * s;
				vt[i].y = scrPos.y + ss.x * s + ss.y * c;
			}

			renderer.SetColorAlphaPremultiplied(col);
			renderer.DrawImage(img, vt[0], vt[1], vt[2], inRect);
		}

		void MapView::DrawText(IFont& font, std::string s, const Vector2& pos, const Vector4& col) {
			Vector2 scrPos;
			if (circularMap) {
				scrPos = Project(pos, true);
				Vector2 rel = scrPos - scrCenter;
				if (rel.GetSquaredLength() > scrRadius*scrRadius)
					return;
			} else if (rotatingMap) {
				scrPos = Project(pos, true);
				if (!outRect.Contains(scrPos))
					return;
			} else {
				if (!inRect.Contains(pos))
					return;
				scrPos = Project(pos);
			}

			Vector2 size = font.Measure(s);
			scrPos.x -= size.x * 0.5F;
			scrPos.y -= size.y;

			// rounded for better pixel alignment
			scrPos.x = floorf(scrPos.x);
			scrPos.y = floorf(scrPos.y);

			font.DrawShadow(s, scrPos, 1.0F, col, MakeVector4(0, 0, 0, col.w));
		}

		void MapView::DrawMapCircle(const Vector2& pos, const Vector4& col, float radius, float thickness) {
			Vector2 scrPos;
			if (circularMap) {
				scrPos = Project(pos, true);
				Vector2 rel = scrPos - scrCenter;
				if (rel.GetSquaredLength() > scrRadius*scrRadius)
					return;
			} else if (rotatingMap) {
				scrPos = Project(pos, true);
				if (!outRect.Contains(scrPos))
					return;
			} else {
				if (!inRect.Contains(pos))
					return;
				scrPos = Project(pos);
			}
			renderer.SetColorAlphaPremultiplied(col);
			renderer.DrawOutlinedCircle(scrPos, radius, thickness);
		}

		void MapView::SwitchScale() {
			scaleMode = (scaleMode + 1) % 4;
			lastScale = actualScale;
			cg_minimapScaleMode = scaleMode;
		}

		std::string MapView::ToGrid(const Vector2& pos) {
			auto letter = char(int('A') + int(pos.x / 64));
			auto number = std::to_string(int(pos.y / 64) + 1);
			return letter + number;
		}

		// definite a palette of 32 color in RGB code
		int palette[32][3] = {
		  {0, 0, 0},	   // 0	 Black			#000000
		  {255, 255, 255}, // 1	 White			#FFFFFF
		  {128, 128, 128}, // 2	 Dark Grey		#808080
		  {255, 255, 0},   // 3	 Yellow			#FFFF00
		  {0, 255, 255},   // 4	 Cyan			#00FFFF
		  {255, 0, 255},   // 5	 Magenta		#FF00FF
		  {255, 0, 0},	   // 6	 Red			#FF0000
		  {0, 255, 0},	   // 7	 Bright Green	#00FF00
		  {0, 0, 255},	   // 8	 Blue			#0000FF
		  {128, 0, 0},	   // 9	 Dark Red		#800000
		  {0, 128, 0},	   // 10 Green			#008000
		  {0, 0, 128},	   // 11 Navy Blue		#000080
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
		  {255, 69, 0},	   // 28 Scarlet		#FF4500
		  {255, 165, 0},   // 29 Light Orange	#FFA500
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
			Vector2 focusPlayerPos;
			float focusPlayerAngle;

			if (isFollowing) {
				int focusedPlayerId = client->GetCameraTargetPlayerId();
				auto maybeTarget = world->GetPlayer(focusedPlayerId);
				if (!maybeTarget)
					return;

				Player& p = maybeTarget.value();
				const auto& pos = p.GetPosition();
				const auto& ori = p.GetFront2D();

				focusPlayerPos = pos.GetXY();
				if (IsThirdPerson(cameraMode)) {
					// In third person, the camera orbits around the player, so we don't set
					// focusPlayerPtr: the player icon is drawn in the players loop
					focusPlayerAngle = client->followAndFreeCameraState.yaw - M_PI_F * 0.5F;
				} else {
					focusPlayerAngle = atan2f(ori.y, ori.x) + M_PI_F * 0.5F;
					focusPlayerPtr = p;
				}
			} else if (isFreeCamera) {
				focusPlayerPos = client->freeCameraState.position.GetXY();
				focusPlayerAngle = client->followAndFreeCameraState.yaw - M_PI_F * 0.5F;
				focusPlayerPtr = world->GetLocalPlayer(); // May be empty in demo mode
			} else {
				return;
			}

			// The local player (this is important for access control)
			// In demo mode, there's no local player - treat as spectator
			stmp::optional<Player&> maybePlayer = world->GetLocalPlayer();

			bool isDemoMode = client->IsDemoMode();
			bool isStaffSpectating = client->staffSpectating;
			bool localPlayerIsSpectator = isDemoMode || (maybePlayer && maybePlayer->IsSpectator());
			bool localPlayerIsSpectating = localPlayerIsSpectator || isStaffSpectating;

			// Need either a local player or demo mode to continue
			if (!maybePlayer && !isDemoMode)
				return;

			// Pointers for safe access (may be null in demo mode)
			Player* localPlayer = maybePlayer ? &maybePlayer.value() : nullptr;
			Player* focusPlayer = focusPlayerPtr ? &focusPlayerPtr.value() : nullptr;

			bool focusPlayerIsLocal = focusPlayer && focusPlayer->IsLocalPlayer();
			bool focusPlayerIsAlive = focusPlayer && focusPlayer->IsAlive();
			bool focusPlayerIsAliveOrSpectator = focusPlayerIsAlive || (focusPlayerIsLocal && localPlayerIsSpectating);

			if (largeMap && zoomState < 0.0001F)
				return;

			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();

			const Handle<GameMap> map = world->GetMap();
			SPAssert(map);

			Vector2 mapSize = MakeVector2((float)map->Width(), (float)map->Height());

			float cfgMapSize = Clamp((float)cg_minimapSize, 32.0F, 256.0F);
			Vector2 mapWndSize = {cfgMapSize, cfgMapSize};

			Vector2 zoomedSize = {512.0F, 512.0F};
			if (sw < zoomedSize.x || sh < zoomedSize.y)
				zoomedSize *= 0.75F;

			Vector2 center = focusPlayerPos;
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

			// The circular/rotating minimap modes only apply to the small corner minimap.
			bool isSofwareRenderer = renderer.IsRendererSW(); // SWRenderer doesn't support minimap clipping (yet)
			this->circularMap = cg_minimapCircular && !isSofwareRenderer && !largeMap;
			this->rotatingMap = cg_minimapRotating && !isSofwareRenderer && !largeMap;

			Vector2 inRange = mapWndSize * 0.5F * actualScale;
			AABB2 inRect(center - inRange, center + inRange);
			if (largeMap) {
				inRect.min = MakeVector2(0, 0);
				inRect.max = mapSize;
			} else if (!rotatingMap) {
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
				if ((statsMode == 2 || (statsMode >= 3 && client->IsScoreboardVisible())) && !isDemoMode)
					winY += cg_statsSmallFont ? 10.0F : 20.0F;
			}

			AABB2 outRect(winX, winY, mapWndSize.x, mapWndSize.y);
			if (largeMap) {
				outRect.min = MakeVector2(sw - zoomedSize.x, sh - zoomedSize.y) * 0.5F;
				outRect.max = MakeVector2(sw + zoomedSize.x, sh + zoomedSize.y) * 0.5F;
			}

			const Vector2& mapHalfSize = (inRect.max - inRect.min) * 0.5F;
			const Vector2& scrHalfSize = (outRect.max - outRect.min) * 0.5F;

			AABB2 tracerClipRect = inRect;

			this->inRect = inRect;
			this->outRect = outRect;
			this->mapCenter = (inRect.min + inRect.max) * 0.5F;
			this->scrCenter = (outRect.min + outRect.max) * 0.5F;
			this->scrRadius = std::min(scrHalfSize.x, scrHalfSize.y);
			this->mapAngle = rotatingMap ? -focusPlayerAngle : 0.0F;
			this->mapRotation = MakeVector2(cosf(mapAngle), sinf(mapAngle));

			float largeMapAlpha = largeMap ? zoomState : 1.0F;
			float alpha = largeMap ? largeMapAlpha : Clamp((float)cg_minimapOpacity, 0.1F, 1.0F);

			if (circularMap) {
				renderer.BeginClippingCircle(scrCenter, scrRadius);

				// draw map
				const auto& topLeft = RotateMap(MakeVector2(-scrHalfSize.x, -scrHalfSize.y));
				const auto& topRight = RotateMap(MakeVector2(scrHalfSize.x, -scrHalfSize.y));
				const auto& bottomLeft = RotateMap(MakeVector2(-scrHalfSize.x, scrHalfSize.y));
				renderer.SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1) * alpha);
				renderer.DrawFlatGameMap(topLeft, topRight, bottomLeft, inRect);

				// draw grid lines
				Vector2 gridSize = mapSize / 8.0F;
				Vector4 gridCol = MakeVector4(0, 0, 0, 1) * 0.4F * alpha;
				DrawGridLines(gridSize, inRect, gridCol, (mapAngle != 0.0F));

				renderer.EndClippingCircle();

				// draw map border
				renderer.SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 1) * alpha);
				renderer.DrawOutlinedCircle(scrCenter, scrRadius + 1.0F, 2.0F);
			} else if (rotatingMap) {
				const Vector2 bigScrHalf = scrHalfSize * static_cast<float>(M_SQRT2);
				const Vector2 bigMapHalf = mapHalfSize * static_cast<float>(M_SQRT2);

				AABB2 bigInRect(mapCenter - bigMapHalf, mapCenter + bigMapHalf);
				tracerClipRect = bigInRect;

				renderer.BeginClippingRect(outRect);

				// draw map
				const auto& topLeft = RotateMap(MakeVector2(-bigScrHalf.x, -bigScrHalf.y));
				const auto& topRight = RotateMap(MakeVector2(bigScrHalf.x, -bigScrHalf.y));
				const auto& bottomLeft = RotateMap(MakeVector2(-bigScrHalf.x, bigScrHalf.y));
				renderer.SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1) * alpha);
				renderer.DrawFlatGameMap(topLeft, topRight, bottomLeft, bigInRect);

				// draw grid lines
				Vector2 gridSize = mapSize / 8.0F;
				Vector4 gridCol = MakeVector4(0, 0, 0, 1) * 0.4F * alpha;
				DrawGridLines(gridSize, bigInRect, gridCol, true);

				renderer.EndClippingRect();

				// draw map border
				renderer.SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 1) * alpha);
				renderer.DrawOutlinedRect(outRect.GetMinX() - 1, outRect.GetMinY() - 1,
										  outRect.GetMaxX() + 1, outRect.GetMaxY() + 1);
			} else {
				// draw map
				renderer.SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1) * alpha);
				renderer.DrawFlatGameMap(outRect, inRect);

				// draw grid lines
				Vector2 gridSize = mapSize / 8.0F;
				Vector4 gridCol = MakeVector4(0, 0, 0, 1) * 0.4F * alpha;
				DrawGridLines(gridSize, inRect, gridCol);

				// draw grid labels
				Handle<IImage> mapFont = renderer.RegisterImage("Gfx/Fonts/MapFont.tga");
				Vector4 labelCol = MakeVector4(1, 1, 1, 1) * 0.8F * alpha;
				for (int i = 0; i < 8; i++) {
					float startX = (float)i * gridSize.x;
					float endX = startX + gridSize.x;
					if (startX > inRect.GetMaxX() || endX < inRect.GetMinX())
						continue;

					float clampedStartX = std::max(startX, inRect.GetMinX());
					float clampedEndX = std::min(endX, inRect.GetMaxX());
					float overlapX = clampedEndX - clampedStartX;
					float fadeX = std::min(overlapX / (endX - startX) * 2.0F, 1.0F);
					renderer.SetColorAlphaPremultiplied(labelCol * fadeX);

					// world-to-screen mapping
					float centerX = (clampedStartX + clampedEndX) * 0.5F;
					float wx = (centerX - inRect.GetMinX()) / inRect.GetWidth();
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

					float clampedStartY = std::max(startY, inRect.GetMinY());
					float clampedEndY = std::min(endY, inRect.GetMaxY());
					float overlapY = clampedEndY - clampedStartY;
					float fadeY = std::min(overlapY / (endY - startY) * 2.0F, 1.0F);
					renderer.SetColorAlphaPremultiplied(labelCol * fadeY);

					// world-to-screen mapping
					float centerY = (clampedStartY + clampedEndY) * 0.5F;
					float wy = (centerY - inRect.GetMinY()) / inRect.GetHeight();
					wy = (wy * outRect.GetHeight()) + outRect.GetMinY();
					wy = roundf(wy); // rounded for better pixel alignment

					float fntX = static_cast<float>((i & 3) * 8);
					float fntY = static_cast<float>((i >> 2) * 8 + 16);
					renderer.DrawImage(mapFont, MakeVector2(outRect.GetMinX() + 4, wy - 4),
									   AABB2(fntX, fntY, 8, 8));
				}

				// draw map border
				renderer.SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 1) * alpha);
				renderer.DrawOutlinedRect(outRect.GetMinX() - 1, outRect.GetMinY() - 1,
										  outRect.GetMaxX() + 1, outRect.GetMaxY() + 1);
			}

			// draw map sector
			int minimapCoords = cg_minimapCoords;
			if (!largeMap && (minimapCoords || rotatingMap)) {
				IFont& font = client->fontManager->GetGuiFont();
				auto gridStr = ToGrid(focusPlayerPos);
				Vector2 size = font.Measure(gridStr);
				Vector2 pos = outRect.min;
				const float gap = 6.0F;
				if (minimapCoords < 2) {
					pos.x += (outRect.GetWidth() - size.x) * 0.5F;
					pos.y = outRect.GetMaxY() + gap - 4.0F;
				} else {
					pos.x = outRect.GetMinX() - gap - size.x;
					pos.y += (outRect.GetHeight() - size.y) * 0.5F;
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

			// draw focused player sound indicators
			if (cg_minimapPlayerSounds && focusPlayerIsAliveOrSpectator) {
				const float scaleX = outRect.GetWidth() / inRect.GetWidth();
				const auto& color = MakeVector4(1, 1, 1, 1);

				for (const auto& indicator : client->GetSoundFeedbackIndicators()) {
					const float fade = Clamp(indicator.fade, 0.0F, 1.0F);
					if (fade <= 0.0F)
						continue;

					// sound can be heard throughout the map, represented as an inner border of the map.
					const float radius = indicator.radius * scaleX;
					if (indicator.farSound || radius > (scrRadius * 1.1F)) {
						const float thickness = 2.0F * fade;
						renderer.SetColorAlphaPremultiplied(color * fade);
						if (circularMap) {
							renderer.DrawOutlinedCircle(scrCenter, scrRadius - thickness * 0.5F, thickness);
						} else {
							renderer.DrawOutlinedRect(outRect.GetMinX(), outRect.GetMinY(),
								outRect.GetMaxX(), outRect.GetMaxY(), (int)floorf(thickness));
						}
						continue;
					}

					DrawMapCircle(focusPlayerPos, color * 0.2F * fade, radius);
				}
			}

			// draw player's icon
			const int iconMode = cg_minimapPlayerIcon;
			const int colorMode = cg_minimapPlayerColor;
			const int namesMode = cg_minimapPlayerNames;

			const auto& spectatorColor = MakeIntVector3(200, 200, 200);
			const auto& localPlayerColor = MakeIntVector3(0, 255, 255);
			const auto& playerTextColor = MakeVector4(1, 1, 1, 0.75F * largeMapAlpha);

			Handle<IImage> spectatorIcon = renderer.RegisterImage("Gfx/Map/Spectator.png");
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
				bool isFocusedPlayer = focusPlayer && &p == focusPlayer;
				if (isFocusedPlayer && !isStaffSpectating)
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
				const auto& pos = p.GetPosition().GetXY();
				const auto& ori = p.GetFront2D();
				const float ang = atan2f(ori.y, ori.x) + M_PI_F * 0.5F;
				DrawIcon(pos, *iconImg, iconColorF, ang);

				// dont draw the focused player name when following non-local players
				if (isFollowingNonLocal && isFocusedPlayer)
					continue;

				// draw player names
				if (namesMode == 1 || (namesMode >= 2 && largeMap))
					DrawText(smallFont, p.GetName(), pos, playerTextColor);
			}

			// draw the focused player view
			float aimDownState = localPlayerIsSpectating ? client->spectatorZoomState : client->GetAimDownState();
			bool isNonLocalPlayerZoomed = (focusPlayer && focusPlayer->IsZoomed()) && !focusPlayerIsLocal;
			bool isZoomed = isNonLocalPlayerZoomed || aimDownState > 0.99F;
			Handle<IImage> focusPlayerViewIcon = isZoomed ? *playerADSViewIcon : *playerViewIcon;

			if (focusPlayer) {
				IntVector3 iconColor = world->GetTeamColor(focusPlayer->GetTeamId());
				if (!focusPlayerIsAlive) {
					iconColor = MakeIntVector3(255, 255, 255);
				} else if (focusPlayerIsLocal) {
					iconColor = localPlayerIsSpectating ? spectatorColor : localPlayerColor;
				} else if (colorMode) {
					int colorIndex = focusPlayer->GetId() % 32;
					iconColor = MakeIntVector3(
						palette[colorIndex][0],
						palette[colorIndex][1],
						palette[colorIndex][2]
					);
				}

				float iconAlpha = (focusPlayerIsAlive ? 1.0F : 0.5F) * largeMapAlpha;
				Vector4 iconColorF = ModifyColor(iconColor) * iconAlpha;

				Handle<IImage> iconImg = playerIcon;
				if (isFreeCamera && localPlayerIsSpectating) {
					iconImg = spectatorIcon;
				} else if (iconMode) {
					switch (focusPlayer->GetWeaponType()) {
						case RIFLE_WEAPON: iconImg = playerRifleIcon; break;
						case SMG_WEAPON: iconImg = playerSMGIcon; break;
						case SHOTGUN_WEAPON: iconImg = playerShotgunIcon; break;
					}
				}

				if (focusPlayerIsAliveOrSpectator)
					DrawIcon(focusPlayerPos, *focusPlayerViewIcon, iconColorF * 0.7F, focusPlayerAngle);
				DrawIcon(focusPlayerPos, *iconImg, iconColorF, focusPlayerAngle);
			} else if (isFreeCamera && localPlayerIsSpectating) {
				// In demo free camera mode, draw a simple view indicator
				Vector4 iconColorF = ModifyColor(spectatorColor) * largeMapAlpha;
				DrawIcon(focusPlayerPos, *focusPlayerViewIcon, iconColorF * 0.7F, focusPlayerAngle);
				DrawIcon(focusPlayerPos, *spectatorIcon, iconColorF, focusPlayerAngle);
			}

			// draw bullet tracers
			renderer.SetColorAlphaPremultiplied(MakeVector4(1, 1, 0, 1) * largeMapAlpha);
			for (const auto& localEntity : client->localEntities) {
				auto* const tracer = dynamic_cast<MapViewTracer*>(localEntity.get());
				if (!tracer)
					continue;

				const auto line1 = tracer->GetLineSegment();
				if (!line1)
					continue;

				auto line2 = ClipLineSegment(std::make_pair(Vector2{(*line1).first.x,
					(*line1).first.y}, Vector2{(*line1).second.x, (*line1).second.y}), tracerClipRect);
				if (!line2)
					continue;

				auto& line3 = *line2;
				line3.first = Project(line3.first, rotatingMap);
				line3.second = Project(line3.second, rotatingMap);
				if (line3.first == line3.second)
					continue;

				if (circularMap) {
					auto clipped = ClipLineSegment(
						{line3.first, line3.second}, scrCenter, scrRadius);
					if (!clipped)
						continue;
					line3 = *clipped;
				} else if (rotatingMap) {
					auto clipped = ClipLineSegment(
						{line3.first, line3.second}, outRect);
					if (!clipped)
						continue;
					line3 = *clipped;
				}

				renderer.DrawLine(line3.first, line3.second);
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
					DrawIcon(team1.basePos.GetXY(), *baseIcon, teamColorF);

					// draw both flags
					if (team2.hasIntel) {
						stmp::optional<Player&> carrier = world->GetPlayer(team2.carrierId);
						if (carrier && (localPlayerIsSpectating || (localPlayer && carrier->IsTeammate(*localPlayer)))) {
							float pulse = std::max(0.5F, fabsf(sinf(world->GetTime() * 4.0F)));
							DrawIcon(carrier->GetPosition().GetXY(), *intelIcon, teamColorF * pulse);
						}
					} else {
						DrawIcon(team1.flagPos.GetXY(), *intelIcon, teamColorF);
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
					DrawIcon(t.pos.GetXY(), *baseIcon, teamColorF);
				}
			}

			DrawTeamplayPings(largeMapAlpha);
			DrawTeamplayMarks(largeMapAlpha);
		}

		void MapView::DrawTeamplayPings(float mapAlpha) {
			const ExtendedTeamplay& teamplay = *client->teamplay;
			if (!teamplay.HasPings())
				return;

			World* world = client->GetWorld();
			if (!world)
				return;

			for (const auto& entry : teamplay.GetPings()) {
				const ExtendedTeamplay::Ping& ping = entry.second;

				// The packet decides where it is shown; this is the minimap.
				if (!(ping.surfaces & ExtendedTeamplay::SurfaceMinimap))
					continue;

				// Match the world marker's fade so a ping does not linger on the minimap
				// after it has gone from the view, or the other way round.
				constexpr float kFadeOutTime = 0.75F;
				float alpha = ping.GetFadeAlpha(kFadeOutTime) * mapAlpha;
				if (alpha <= 0.0F)
					continue;

				// The colour the server chose, drawn as sent.
				Vector3 pingCol = ExtendedTeamplay::ToRenderColor(ping.color);
				Vector4 color = MakeVector4(pingCol.x, pingCol.y, pingCol.z, alpha);
				color.x *= alpha;
				color.y *= alpha;
				color.z *= alpha;

				// A ring that shrinks as the ping ages, so a fresh callout catches the
				// eye and an old one does not compete with the player icons.
				constexpr float kOuterRadius = 7.0F;
				constexpr float kInnerRadius = 2.5F;
				float radius = Mix(kOuterRadius, kInnerRadius, ping.GetAgeFraction());

				// The map is flat, so a ping's height plays no part in where it lands.
				const Vector2 pingPos = ping.position.GetXY();
				DrawMapCircle(pingPos, color, radius, 1.5F);
				DrawMapCircle(pingPos, color, kInnerRadius * 0.5F, 1.5F);
			}
		}

		void MapView::DrawTeamplayMarks(float mapAlpha) {
			const ExtendedTeamplay& teamplay = *client->teamplay;
			if (!teamplay.HasMarks())
				return;

			World* world = client->GetWorld();
			if (!world)
				return;

			for (const auto& entry : teamplay.GetMarks()) {
				const ExtendedTeamplay::Mark& mark = entry.second;
				if (!(mark.surfaces & ExtendedTeamplay::SurfaceMinimap))
					continue;

				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(entry.first));
				if (!maybePlayer)
					continue;

				Player& p = maybePlayer.value();
				if (p.IsSpectator() || !p.IsAlive())
					continue;

				// A mark on the minimap is a dot at the player, in the mark's colour —
				// the same colour the outline uses, blink and all, so every surface the
				// mark named says the same thing at the same moment.
				Vector3 col = client->ResolveMarkColor(p, mark.color);

				Vector4 color = MakeVector4(col.x * mapAlpha, col.y * mapAlpha,
											col.z * mapAlpha, mapAlpha);

				constexpr float kRadius = 4.0F;
				DrawMapCircle(p.GetPosition().GetXY(), color, kRadius, 1.5F);
			}
		}

		MapViewTracer::MapViewTracer(Vector3 p1, Vector3 p2)
			: startPos(p1), velocity(300.0F) {
			// Z coordinate doesn't matter in MapView
			p1.z = p2.z = 0.0F;

			dir = p2 - p1;
			length = dir.GetLength();
			dir = dir.Normalize();

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

			Vector3 p1 = startPos + dir * startDist;
			Vector3 p2 = startPos + dir * endDist;
			return std::make_pair(p1, p2);
		}

		MapViewTracer::~MapViewTracer() {}
	} // namespace client
} // namespace spades