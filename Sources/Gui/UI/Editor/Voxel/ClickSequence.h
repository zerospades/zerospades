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

#include <vector>

#include <Core/Math.h>

namespace spades {
	namespace gui {
		/**
		 * Shared multi-click gesture state for tools that build a shape from a fixed
		 * number of clicked points (e.g. Rect = corner, opposite corner, depth).
		 *
		 * It owns only the bookkeeping that every such tool repeats — the ordered
		 * points, completion once `count` points are in, and cancellation. How a
		 * click maps to a point, how the points become voxels, and how the
		 * in-progress shape previews stay in the tool, because those differ per shape.
		 */
		class ClickSequence {
		public:
			// Begin collecting `count` points; the gesture completes once they're in.
			void BeginFixed(int count) {
				Reset();
				target = count;
				active = true;
			}

			bool Active() const { return active; }
			int Count() const { return int(points.size()); }
			const std::vector<IntVector3>& Points() const { return points; }

			// Record a clicked point. Returns true once `count` points have been
			// collected (the gesture is complete).
			bool Add(const IntVector3& p) {
				if (!active || completed)
					return false;
				points.push_back(p);
				if (int(points.size()) >= target)
					completed = true;
				return completed;
			}

			void Reset() {
				points.clear();
				active = false;
				completed = false;
			}

		private:
			int target = 0;
			bool active = false;
			bool completed = false;
			std::vector<IntVector3> points;
		};
	} // namespace gui
} // namespace spades
