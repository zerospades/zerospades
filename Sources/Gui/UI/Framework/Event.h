/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <functional>
#include <string>

namespace spades {
	namespace gui {
		namespace ui {
			class UIElement;
			class Timer;

			enum class MouseButton {
				None,
				Left,
				Right,
				Middle,
				Button4,
				Button5
			};

			/** Handler invoked for an event raised by a UI element. */
			using EventHandler = std::function<void(UIElement&)>;

			/** Handler invoked with pasted clipboard text. */
			using PasteClipboardEventHandler = std::function<void(const std::string&)>;

			/** Handler invoked when a repeated key/character fires. */
			using KeyRepeatEventHandler = std::function<void(const std::string&)>;

			/** Handler invoked when a timer ticks. */
			using TimerTickEventHandler = std::function<void(Timer&)>;
		} // namespace ui
	} // namespace gui
} // namespace spades
