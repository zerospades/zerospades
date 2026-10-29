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
			       tanHalfFovY > 0.0F;
		}

		float GizmoView::Depth(const Vector3& world) const {
			return Vector3::Dot(world - eye, forward);
		}

		bool GizmoView::Project(const Vector3& world, Vector2& screen) const {
			if (!IsValid())
				return false;
			Vector3 rel = world - eye;
			float z = Vector3::Dot(rel, forward);
			if (z <= kNearLimit)
				return false;
			float sx = (Vector3::Dot(rel, right) / z) / tanHalfFovX;
			float sy = -(Vector3::Dot(rel, up) / z) / tanHalfFovY;
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
			Vector3 d = forward + right * (sx * tanHalfFovX) - up * (sy * tanHalfFovY);
			direction = d.Normalize(); // never zero: `forward` is a unit vector
		}

		float GizmoView::WorldPerPixel(const Vector3& world) const {
			if (!IsValid())
				return 0.0F;
			float z = std::max(Depth(world), kNearLimit);
			return 2.0F * z * tanHalfFovY / viewportHeight;
		}
	} // namespace gui
} // namespace spades
