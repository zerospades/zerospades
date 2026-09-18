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
	namespace client {
		class IRenderer;

		/**
		 * Draws the *Teamplay* ping marker: a diamond with a dark outline and a soft
		 * drop shadow, a light rim around a body in the ping's colour, and a bright
		 * core. Every surface a ping can appear on — the world, the minimap and the
		 * compass — draws it through this, so a ping looks the same wherever it is.
		 *
		 * `center` is snapped to the pixel grid so the edges stay crisp while the
		 * marker moves. `halfSize` is the distance from the centre to a tip of the
		 * coloured body; the outline and shadow extend past it by
		 * `GetPingDiamondExtent(halfSize) - halfSize`. `color` is straight (not
		 * premultiplied) RGB and `alpha` the opacity of the whole marker.
		 *
		 * `age` is the seconds since the ping was placed, and drives the animation:
		 * the marker pops in with an overshoot, sends out a burst of expanding diamond
		 * rings, then keeps a slow, softer pulse and a breathing core for as long as it
		 * lives. The rings reach past the extent while they play.
		 */
		void DrawPingDiamond(IRenderer& renderer, Vector2 center, float halfSize,
							 const Vector3& color, float alpha, float age);

		/** The distance from the centre to a tip of everything `DrawPingDiamond` draws
		 * for `halfSize`, for placing a label or margin clear of the marker. */
		float GetPingDiamondExtent(float halfSize);
	} // namespace client
} // namespace spades
