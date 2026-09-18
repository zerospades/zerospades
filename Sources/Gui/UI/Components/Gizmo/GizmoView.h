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

#include <Core/Math.h>

namespace spades {
	namespace gui {
		/**
		 * A perspective camera as seen by an on-screen manipulator: enough to put a
		 * world point on the screen and to turn a screen point back into a ray.
		 *
		 * It carries no renderer or scene state, so any 3D view (model editor, map
		 * editor, ...) can describe itself with one and hand it to a gizmo.
		 */
		struct GizmoView {
			Vector3 eye = MakeVector3(0.0F, 0.0F, 0.0F);
			// Orthonormal camera frame. `up` is screen-up, `forward` looks into the
			// scene.
			Vector3 right = MakeVector3(1.0F, 0.0F, 0.0F);
			Vector3 up = MakeVector3(0.0F, 1.0F, 0.0F);
			Vector3 forward = MakeVector3(0.0F, 0.0F, 1.0F);
			float tanHalfFovX = 1.0F;
			float tanHalfFovY = 1.0F;
			// Viewport in screen pixels.
			float viewportX = 0.0F;
			float viewportY = 0.0F;
			float viewportWidth = 0.0F;
			float viewportHeight = 0.0F;

			/** False when the view is degenerate and nothing can be projected. */
			bool IsValid() const;

			/** Distance of `world` in front of the eye, along `forward`. */
			float Depth(const Vector3& world) const;

			/**
			 * Screen position of `world`. False, with `screen` untouched, when the
			 * point is at or behind the near limit and has no meaningful position.
			 */
			bool Project(const Vector3& world, Vector2& screen) const;

			/** The ray from the eye through screen point `screen` (unit direction). */
			void Ray(const Vector2& screen, Vector3& origin, Vector3& direction) const;

			/** World length that covers one screen pixel at the depth of `world`. */
			float WorldPerPixel(const Vector3& world) const;
		};
	} // namespace gui
} // namespace spades
