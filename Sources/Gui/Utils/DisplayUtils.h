/*
 Copyright (c) 2026 ZeroSpades contributors

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

#include <string>
#include <vector>

struct SDL_Window;

namespace spades {
	namespace gui {
		/**
		 * Monitor helpers built only on SDL2 calls available since 2.0.0, so they behave
		 * the same on Windows, macOS and Linux (X11 and Wayland).
		 *
		 * Monitors are remembered by `r_videoDisplay` (SDL index) and
		 * `r_videoDisplayName`: SDL indices shift when a monitor is plugged in or
		 * removed, so the name is tried first and the index is the fallback.
		 */
		namespace display {
			struct Resolution {
				int width;
				int height;
			};

			/** Number of connected monitors (at least 1 once SDL video is up). */
			int GetNumDisplays();

			/** The monitor's name as SDL reports it, or "Display N" when it has none. */
			std::string GetDisplayName(int index);

			/**
			 * The monitor the window is shown on (the one holding the window's centre).
			 * Falls back to the remembered monitor, then to 0.
			 */
			int GetWindowDisplay(SDL_Window* window);

			/** The remembered monitor, if it is still connected; otherwise 0. */
			int GetPreferredDisplay();

			/** Remembers `index` as the monitor the game window opens on. */
			void SetPreferredDisplay(int index);

			/**
			 * Whether the game uses the desktop resolution of its monitor
			 * (`r_videoAutoResolution`, on by default) instead of r_videoWidth/Height.
			 */
			bool IsAutoResolution();
			void SetAutoResolution(bool on);

			/** The monitor's current desktop resolution; false if SDL can't tell. */
			bool GetDesktopResolution(int index, Resolution& out);

			/**
			 * The monitor's distinct video modes of at least 640x480, largest first.
			 * Never empty: a stock list is returned when SDL reports nothing.
			 */
			std::vector<Resolution> GetVideoModes(int index);
		} // namespace display
	} // namespace gui
} // namespace spades
