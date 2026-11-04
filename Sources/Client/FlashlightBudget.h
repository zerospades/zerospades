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

#include <cstddef>
#include <vector>

#include "IRenderer.h"
#include "SceneDefinition.h"
#include <Core/Math.h>

namespace spades {
	namespace client {
		class GameMap;

		/**
		 * Which lit flashlights light the scene as spotlights this frame.
		 *
		 * A spotlight costs a pass over everything it reaches and a walk through the
		 * map for its shadows, so with a crowd of lit players they can't all be
		 * drawn so. The beams that matter most to the camera are, up to
		 * `MaxLitBeams`: the one of the player the camera looks through first, then
		 * those lighting the view, nearest first. Every other beam in view shows as a
		 * glow where it lands, as the Flashlight extension lets a client approximate
		 * a light, in its own colour and over its own reach and cone, and on the
		 * blocks that stop it, so that the glow never shows through a wall.
		 */
		class FlashlightBudget {
		public:
			/** The most beams lit as spotlights in a frame. */
			static constexpr std::size_t MaxLitBeams = 16;

			/** A lit flashlight's beam, as it is this frame. */
			struct Beam {
				/** Its spotlight */
				DynamicLightParam light;
				/** Linear, what a glow where it lands is drawn with */
				Vector3 glowColor;
				/** Whether the camera looks through its player's eyes */
				bool viewed = false;
			};

			void Clear() { beams.clear(); }
			void Offer(const Beam& beam) { beams.push_back(beam); }

			/** Adds this frame's beams to the scene `view` shows, as spotlights or as
			 * glows, and forgets them. */
			void Submit(IRenderer&, const GameMap&, const SceneDefinition& view);

		private:
			std::vector<Beam> beams;
			std::vector<std::size_t> order;

			/** Adds the glow where `beam` lands, if it lands within its reach. */
			void AddGlow(IRenderer&, const GameMap&, const Beam& beam);
		};
	} // namespace client
} // namespace spades
