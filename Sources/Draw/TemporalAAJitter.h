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

#include <Core/Math.h>

namespace spades {
	namespace draw {
		/**
		 * The sub-pixel offsets temporal AA (`r_temporalAA`) draws the main view at,
		 * one per frame in turn, the same for every renderer: their temporal AA
		 * resolve is tuned for this sequence and its spread.
		 */
		class TemporalAAJitter {
			std::size_t index = 0;

		public:
			/**
			 * The next offset, in normalized device coordinates for a view of
			 * `width` by `height` pixels: within about 0.7 of a pixel of the centre.
			 */
			Vector2 Next(int width, int height);
		};
	} // namespace draw
} // namespace spades
