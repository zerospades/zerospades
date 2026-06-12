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
		 * Shared multi-click gesture state for tools that build a shape from several
		 * clicked points (a box, a line, a curve...).
		 *
		 * It owns only the bookkeeping that every such tool repeats — the ordered
		 * points, how the gesture completes, and cancellation. How a click maps to a
		 * point, how the points become voxels, and how the in-progress shape previews
		 * stay in the tool, because those differ per shape.
		 *
		 * Fixed mode completes automatically once `count` points are recorded (e.g.
		 * Rect = 3). Variable mode collects points until the tool calls Finish() from
		 * a terminator gesture (double-click / Enter / RMB), requiring at least
		 * `minPoints` (e.g. a curve).
		 */
		class ClickSequence {
		public:
			void BeginFixed(int count) {
				Reset();
				mode = Mode::Fixed;
				target = count;
				active = true;
			}
			void BeginVariable(int minPoints = 2) {
				Reset();
				mode = Mode::Variable;
				minPts = minPoints;
				active = true;
			}

			bool Active() const { return active; }
			bool Completed() const { return completed; }
			int Count() const { return int(points.size()); }
			int Target() const { return mode == Mode::Fixed ? target : -1; }
			const std::vector<IntVector3>& Points() const { return points; }
			const IntVector3& Last() const { return points.back(); } // needs Count() > 0

			// Record a clicked point. Returns true if the gesture is now Completed
			// (Fixed reached its count). Variable never auto-completes here.
			bool Add(const IntVector3& p) {
				if (!active || completed)
					return false;
				points.push_back(p);
				if (mode == Mode::Fixed && int(points.size()) >= target)
					completed = true;
				return completed;
			}

			// Variable mode: complete via a terminator gesture. Returns true if it
			// completed (had at least `minPoints`).
			bool Finish() {
				if (!active || completed)
					return false;
				if (mode == Mode::Variable && int(points.size()) >= minPts)
					completed = true;
				return completed;
			}

			void Reset() {
				points.clear();
				active = false;
				completed = false;
			}

		private:
			enum class Mode { Fixed, Variable };
			Mode mode = Mode::Fixed;
			int target = 0;
			int minPts = 2;
			bool active = false;
			bool completed = false;
			std::vector<IntVector3> points;
		};
	} // namespace gui
} // namespace spades
