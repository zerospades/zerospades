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

#include <string>

#include <Core/Math.h>
#include <Gui/UI/Components/Gizmo/GizmoView.h>

namespace spades {
	namespace gui {
		/**
		 * What a VoxelEditor needs from the view hosting it: how the document is
		 * seen and aimed at. The host owns the camera, draws the document and
		 * routes input; the editor owns everything tools touch.
		 */
		class IVoxelEditorHost {
		public:
			virtual ~IVoxelEditorHost() {}

			/** The view the last frame was drawn with: picking, projection and
			 *  gizmos all go through it, so they agree on every pixel. */
			virtual const GizmoView& CurrentView() const = 0;
			/** Where tools aim, in screen pixels: the cursor, or the crosshair. */
			virtual const Vector2& AimPosition() const = 0;

			/** Keys the host acts on itself (the camera's, the screenshot key),
			 *  which no tool may take as its hot key. */
			virtual bool KeyIsReserved(const std::string& key) const = 0;
			/** A click the host's own widgets on the viewport (a navigation
			 *  cube) take before any tool sees it; false when none did. */
			virtual bool ClickViewportWidget(const Vector2& cursor) {
				(void)cursor;
				return false;
			}

			/** The document was relabelled by `shift`: whatever the host keeps in
			 *  voxel coordinates (its camera) follows, so the view stays put. */
			virtual void DocumentReframed(const Vector3& shift) = 0;
			/** Something took the input over: forget held keys (movement). */
			virtual void ReleaseHeldKeys() = 0;
		};
	} // namespace gui
} // namespace spades
