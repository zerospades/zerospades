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

namespace spades {
	namespace draw {
		/**
		 * How many frames are recorded while the GPU may still be drawing earlier
		 * ones: the frame slots of the swapchain's fences and command buffers, and
		 * of every resource kept per frame in flight. The one count all of them
		 * are sized from.
		 */
		constexpr int MaxFramesInFlight = 2;
	} // namespace draw
} // namespace spades
