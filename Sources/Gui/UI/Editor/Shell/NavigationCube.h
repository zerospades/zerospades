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
#include <Gui/UI/Components/Gizmo/GizmoView.h>

namespace spades {
	namespace client {
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * A FreeCAD-style navigation cube: a chamfered cube turning with the
		 * view, whose faces, edges and corners are clickable to look from that
		 * side (face / bevel edge / corner -> ortho / 45 degrees / isometric),
		 * with a home button off its bottom left to go back to the opening view.
		 */
		class NavigationCube {
		public:
			/** Places the cube: its centre on screen, and its radius in pixels. */
			void Layout(float centreX, float centreY, float radius);

			/** The direction to look from for the spot `p` on the cube, as seen
			 *  through `view`; false when `p` is not over the cube. */
			bool DirectionAt(const GizmoView& view, const Vector2& p, Vector3& dir) const;
			/** Whether `p` is over the home button, off the cube's bottom left. */
			bool HomeAt(const Vector2& p) const;

			/** Draws the cube as `view` sees it, lighting the facet under `cursor`. */
			void Draw(client::IRenderer& renderer, client::IFont& font, const GizmoView& view,
			          const Vector2& cursor) const;

		private:
			float cx = 0.0F, cy = 0.0F, r = 0.0F;

			// A vertex of the cube (in [-1, 1]^3) on screen, as `view` turns it.
			Vector2 Project(const GizmoView& view, const Vector3& v) const;
			// The home button's centre on screen.
			Vector2 HomeCentre() const;
			void DrawHome(client::IRenderer& renderer, bool hovered) const;
		};
	} // namespace gui
} // namespace spades
