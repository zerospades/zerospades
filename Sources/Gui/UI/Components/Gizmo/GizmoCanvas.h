/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <vector>

#include <Core/Math.h>

namespace spades {
	namespace client {
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * Flat 2D shapes in screen pixels, drawn over the finished scene.
		 *
		 * The primitives a manipulator needs — thick lines, polylines, filled convex
		 * polygons, discs and circles — anti-aliased by the overlay helpers.
		 * Colours use straight (non-premultiplied) alpha.
		 */
		class GizmoCanvas {
		public:
			explicit GizmoCanvas(client::IRenderer& renderer);

			/** Hard-edged on purpose, so a fan of triangles tiles a larger shape
			 *  without the seams anti-aliased edges would leave between them. */
			void Triangle(const Vector2& a, const Vector2& b, const Vector2& c,
			              const Vector4& color);
			/** Fills a convex polygon given in either winding order. */
			void Convex(const Vector2* points, int count, const Vector4& color);
			void Line(const Vector2& a, const Vector2& b, float width, const Vector4& color);
			/** Consecutive segments mitred at the joints, so a translucent stroke
			 *  doesn't darken where they meet; `closed` links last to first. */
			void Polyline(const std::vector<Vector2>& points, float width, const Vector4& color,
			              bool closed);
			void Disc(const Vector2& center, float radius, const Vector4& color);
			/** A ring of the given stroke width centred on `radius`. */
			void Circle(const Vector2& center, float radius, float width, const Vector4& color);

		private:
			client::IRenderer& renderer;
		};
	} // namespace gui
} // namespace spades
