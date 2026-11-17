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

#include "GizmoView.h"

#include <algorithm>

namespace spades {
	namespace gui {
		namespace {
			// Points closer to the eye than this have no stable screen position.
			constexpr float kNearLimit = 1.0e-3F;
		} // namespace

		bool GizmoView::IsValid() const {
			return viewportWidth > 0.0F && viewportHeight > 0.0F && tanHalfFovX > 0.0F &&
			       tanHalfFovY > 0.0F && (!orthogonal || orthoHeight > 0.0F);
		}

		float GizmoView::HalfHeightAt(float depth) const {
			// The one place the two projections differ: how far across the view
			// reaches at a depth. Everything below is written in terms of it, so
			// a parallel view is the same arithmetic with the depth left out.
			return orthogonal ? orthoHeight * 0.5F : depth * tanHalfFovY;
		}

		float GizmoView::Depth(const Vector3& world) const {
			return Vector3::Dot(world - eye, forward);
		}

		bool GizmoView::Project(const Vector3& world, Vector2& screen) const {
			if (!IsValid())
				return false;
			Vector3 rel = world - eye;
			float z = Vector3::Dot(rel, forward);
			// A point level with the eye or behind it has no screen position in
			// a perspective view. A parallel one has no eye to be behind, but a
			// point behind the viewer is still not in front of them.
			if (z <= kNearLimit && !orthogonal)
				return false;
			const float halfHeight = HalfHeightAt(z);
			const float halfWidth = halfHeight * (tanHalfFovX / tanHalfFovY);
			if (halfHeight <= 0.0F || halfWidth <= 0.0F)
				return false;
			float sx = Vector3::Dot(rel, right) / halfWidth;
			float sy = -Vector3::Dot(rel, up) / halfHeight;
			screen = MakeVector2(viewportX + (sx + 1.0F) * 0.5F * viewportWidth,
			                     viewportY + (sy + 1.0F) * 0.5F * viewportHeight);
			return true;
		}

		void GizmoView::Ray(const Vector2& screen, Vector3& origin, Vector3& direction) const {
			origin = eye;
			if (!IsValid()) {
				direction = forward;
				return;
			}
			float sx = ((screen.x - viewportX) / viewportWidth) * 2.0F - 1.0F;
			float sy = ((screen.y - viewportY) / viewportHeight) * 2.0F - 1.0F;
			if (orthogonal) {
				// Every ray looks the same way; where it starts is what differs,
				// so the screen point moves the eye across the view plane.
				const float halfHeight = HalfHeightAt(0.0F);
				const float halfWidth = halfHeight * (tanHalfFovX / tanHalfFovY);
				origin = eye + right * (sx * halfWidth) - up * (sy * halfHeight);
				direction = forward;
				return;
			}
			Vector3 d = forward + right * (sx * tanHalfFovX) - up * (sy * tanHalfFovY);
			direction = d.Normalize(); // never zero: `forward` is a unit vector
		}

		float GizmoView::WorldPerPixel(const Vector3& world) const {
			if (!IsValid())
				return 0.0F;
			float z = std::max(Depth(world), kNearLimit);
			return 2.0F * HalfHeightAt(z) / viewportHeight;
		}
	} // namespace gui
} // namespace spades
