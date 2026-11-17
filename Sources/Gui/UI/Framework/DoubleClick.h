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

namespace spades {
	namespace gui {
		namespace ui {
			/**
			 * Tells a double click from two clicks, the same way everywhere in
			 * the game's UI: the second soon after the first, close to it, and
			 * on the same thing.
			 */
			class DoubleClick {
			public:
				/** Seconds the second click may follow the first in. */
				static constexpr float kInterval = 0.35F;
				/** How far (Manhattan, in UI units) it may land from the first. */
				static constexpr float kReach = 10.0F;

				/**
				 * A click at `at`, at `time` seconds, on `target` (whatever names
				 * what was clicked to its owner; empty when there is one thing).
				 * True when it makes a double click with the last one; a double
				 * click is then over, so a third click starts afresh.
				 */
				bool Click(const Vector2& at, float time, const std::string& target = std::string());
				/** Forgets the last click: what follows is a first one. */
				void Reset() { lastTime = -1.0e9F; }

			private:
				float lastTime = -1.0e9F;
				Vector2 lastAt = MakeVector2(0.0F, 0.0F);
				std::string lastTarget;
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
