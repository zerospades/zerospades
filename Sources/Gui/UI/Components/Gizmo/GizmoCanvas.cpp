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

#include "GizmoCanvas.h"

#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>

namespace spades {
	namespace gui {
		GizmoCanvas::GizmoCanvas(client::IRenderer& renderer) : renderer(renderer) {}

		void GizmoCanvas::Triangle(const Vector2& a, const Vector2& b, const Vector2& c,
		                           const Vector4& color) {
			OverlayColorNP(renderer, color);
			renderer.DrawFilledTriangle(a, b, c);
		}

		void GizmoCanvas::Convex(const Vector2* points, int count, const Vector4& color) {
			if (count >= 3)
				OverlayFillConvexPolygon(renderer, points, std::size_t(count), color);
		}

		void GizmoCanvas::Line(const Vector2& a, const Vector2& b, float width,
		                       const Vector4& color) {
			OverlayStrokeLine(renderer, a, b, width, color);
		}

		void GizmoCanvas::Polyline(const std::vector<Vector2>& points, float width,
		                           const Vector4& color, bool closed) {
			OverlayStrokePolyline(renderer, points.data(), points.size(), width, color, closed);
		}

		void GizmoCanvas::Disc(const Vector2& center, float radius, const Vector4& color) {
			OverlayFillCircle(renderer, center, radius, color);
		}

		void GizmoCanvas::Circle(const Vector2& center, float radius, float width,
		                         const Vector4& color) {
			OverlayStrokeCircle(renderer, center, radius, width, color);
		}
	} // namespace gui
} // namespace spades
