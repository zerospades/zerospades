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

namespace spades {
	namespace gui {
		/**
		 * Modal menu interface — Esc/pause menu for both game and editor contexts.
		 * Implementations handle menu state, input, sound, and rendering.
		 */
		class IModalMenu {
		public:
			virtual ~IModalMenu() = default;

			// State
			virtual bool IsActive() const = 0;
			virtual void Open() = 0;
			virtual void Close() = 0;

			// Input
			virtual bool KeyEvent(const std::string& key, bool down) = 0;
			virtual void TextInputEvent(const std::string& text) = 0;
			virtual bool AcceptsTextInput() const = 0;

			// Rendering
			virtual void Draw() = 0;
		};
	} // namespace gui
} // namespace spades
