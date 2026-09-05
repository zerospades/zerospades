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

#pragma once

#include <utility>

#include "ILocalEntity.h"
#include <Core/Math.h>
#include <Core/TMPUtils.h>

namespace spades {
	namespace client {
		extern int palette[32][3];
		class Client;
		class IRenderer;
		class IImage;
		class MapView {
			Client* client;
			IRenderer& renderer;

			int scaleMode;
			float actualScale;
			float lastScale; // used for animation

			float zoomState;
			bool zoomed;

			bool largeMap;
			bool circularMap;
			bool rotatingMap;

			AABB2 inRect;	// world-space
			AABB2 outRect;	// screen-space
			Vector2 mapCenter;
			Vector2 scrCenter;
			float scrRadius;
			float mapAngle;
			Vector2 mapRotation;

			Vector2 RotateMap(const Vector2& offset) const;
			Vector2 Project(const Vector2&, bool rotated = false) const;

			void DrawGridLines(const Vector2&, const AABB2&, const Vector4&, bool rotates = false);
			void DrawIcon(const Vector2&, IImage& img, const Vector4&, float rotation = 0.0F);
			void DrawText(IFont& font, std::string s, const Vector2&, const Vector4&);
			void DrawMapCircle(const Vector2&, const Vector4&, float radius, float thickness = 1.0F);

			/** Relayed *Extended Teamplay* pings, when the server permits them on the
			 * minimap. `mapAlpha` is the large map's fade-in factor. */
			void DrawTeamplayPings(float mapAlpha);

			/** Players the server marked, when the mark names the minimap surface. */
			void DrawTeamplayMarks(float mapAlpha);

		public:
			MapView(Client*, bool largeMap);
			~MapView();

			void Update(float dt);
			void SwitchScale();
			bool IsZoomed() { return zoomed; }
			void SetZoom(bool value) { zoomed = value; }
			std::string ToGrid(const Vector2&);

			void Draw();
		};

		class MapViewTracer : public ILocalEntity {
			Vector3 startPos, dir;
			float length;
			float curDistance;
			float visibleLength;
			float velocity;
			bool firstUpdate;

		public:
			MapViewTracer(Vector3 p1, Vector3 p2);
			~MapViewTracer();

			bool Update(float dt) override;

			stmp::optional<std::pair<Vector3, Vector3>> GetLineSegment();
		};
	} // namespace client
} // namespace spades