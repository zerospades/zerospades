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
#include <Core/TMPUtils.h>

namespace spades {
	namespace client {
		class GameMap;
		class IRenderer;

		/**
		 * The dazzle of a lit lamp as the camera sees it.
		 *
		 * It is the light the eye scatters around a source too bright for it: a
		 * white-hot core in a glow with a long, edgeless tail, which widens as the
		 * camera turns down the beam and as the scene gets darker. It is drawn over
		 * the finished frame, as it happens in the eye and not in the world, and
		 * only the map standing between the camera and the lamp hides it: players
		 * and models don't, so that every client gives a lit player away alike. It
		 * is drawn under the camera's own hands and weapon, like the rest of the
		 * screen in front of the eye.
		 *
		 * `Update` and then `AddToScene` run while the scene is built.
		 */
		class FlashlightGlare {
		public:
			/** A lit lamp, as it is this frame. */
			struct Lamp {
				Vector3 position;
				Vector3 direction; // the beam's axis, normalized
				float coneAngle;   // full angle, in radians
				float reach;       // blocks
				Vector3 color;     // linear
				float brightness;  // 0 to 1, its fade-in after being switched on
			};

			/**
			 * Works out this frame's glare of `lamp` as seen from `viewOrigin`.
			 * `time` is in seconds; it only has to increase from frame to frame.
			 */
			void Update(const Lamp& lamp, const Vector3& viewOrigin, const GameMap& map,
			            float time);

			/** Drops this frame's glare, for a lamp that is off or not drawn. */
			void Clear() { frame.reset(); }

			/**
			 * Adds this frame's glare to the scene being built. `ambient` is how
			 * bright the scene around it is, from `0` for total darkness, where the
			 * glare is at its widest, to `1` for full daylight.
			 */
			void AddToScene(IRenderer&, float ambient) const;

		private:
			struct Frame {
				Vector3 position;
				Vector3 color;
				float reception; // 0 to 1: how much of the light reaches the camera
				float beam;      // 1 on the beam's axis, 0 at its edge and outside it
				float lens;      // 1 facing the lens squarely, 0 seeing it edge-on
			};
			stmp::optional<Frame> frame;

			/** How much of the lamp the camera sees, eased so that the glare doesn't
			 * pop at a corner, and the `time` it was last eased at. */
			float visibility = 0.0F;
			float visibilityTime = 0.0F;
		};
	} // namespace client
} // namespace spades
