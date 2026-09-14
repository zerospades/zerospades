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
		// Typed editor input, replacing the old stringly-typed button names and the
		// separate Down/Up/Drag callbacks. The host (KV6EditorView) translates raw
		// SDL/View events into these and feeds them to the active tool. Keeping the
		// shape small and POD-like makes it cheap to forward and easy to expose to
		// scripts later.

		enum class PointerButton { None, Left, Right, Middle };

		enum class PointerPhase {
			Down, // button pressed
			Up,   // button released
			Move, // cursor moved with no button held
			Drag  // cursor moved with a button held (button = the held one)
		};

		struct PointerInput {
			PointerButton button = PointerButton::None;
			PointerPhase phase = PointerPhase::Move;
			Vector2 pos = MakeVector2(0, 0);   // cursor position, screen pixels
			Vector2 delta = MakeVector2(0, 0); // movement since the last event
			bool alt = false, ctrl = false, shift = false;

			bool IsDown() const { return phase == PointerPhase::Down; }
			bool IsUp() const { return phase == PointerPhase::Up; }
			bool IsDrag() const { return phase == PointerPhase::Drag; }
			bool IsLeft() const { return button == PointerButton::Left; }
			bool IsRight() const { return button == PointerButton::Right; }
		};

		enum class KeyPhase { Down, Up };

		struct KeyInput {
			std::string key;
			KeyPhase phase = KeyPhase::Down;
			bool alt = false, ctrl = false, shift = false;

			bool IsDown() const { return phase == KeyPhase::Down; }
		};
	} // namespace gui
} // namespace spades
