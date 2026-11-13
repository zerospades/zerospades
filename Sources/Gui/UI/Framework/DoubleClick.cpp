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

#include "DoubleClick.h"

namespace spades {
	namespace gui {
		namespace ui {
			bool DoubleClick::Click(const Vector2& at, float time, const std::string& target) {
				const bool twice = time - lastTime < kInterval &&
				                   (at - lastAt).GetManhattanLength() < kReach && target == lastTarget;
				lastTime = twice ? -1.0e9F : time;
				lastAt = at;
				lastTarget = target;
				return twice;
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
