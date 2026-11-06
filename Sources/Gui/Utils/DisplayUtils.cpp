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

#include <algorithm>
#include <set>
#include <utility>

#include "DisplayUtils.h"

#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Imports/SDL.h>

DEFINE_SPADES_SETTING(r_videoDisplay, "0");
DEFINE_SPADES_SETTING(r_videoDisplayName, "");
// Set once the setup screen has filled in the detected resolution on first run.
DEFINE_SPADES_SETTING(r_videoResolutionDetected, "0");

namespace spades {
	namespace gui {
		namespace display {
			int GetNumDisplays() { return std::max(SDL_GetNumVideoDisplays(), 0); }

			std::string GetDisplayName(int index) {
				const char* name = SDL_GetDisplayName(index);
				if (name && *name)
					return name;
				return "Display " + std::to_string(index + 1);
			}

			int GetPreferredDisplay() {
				int count = GetNumDisplays();
				if (count <= 0)
					return 0;

				// The name survives monitors being plugged in or out; the index doesn't.
				std::string name = r_videoDisplayName;
				if (!name.empty()) {
					for (int i = 0; i < count; i++) {
						const char* n = SDL_GetDisplayName(i);
						if (n && name == n)
							return i;
					}
				}

				int index = r_videoDisplay;
				return (index >= 0 && index < count) ? index : 0;
			}

			void SetPreferredDisplay(int index) {
				if (index < 0 || index >= GetNumDisplays())
					return;
				r_videoDisplay = index;
				const char* name = SDL_GetDisplayName(index);
				r_videoDisplayName = std::string(name ? name : "");
			}

			int GetWindowDisplay(SDL_Window* window) {
				if (window) {
					// SDL picks the display holding the window's centre (the closest one
					// if the centre is off-screen), so a window straddling two monitors
					// resolves to the one showing most of it.
					int index = SDL_GetWindowDisplayIndex(window);
					if (index >= 0 && index < GetNumDisplays())
						return index;
					SPLog("Couldn't tell which display the window is on: %s", SDL_GetError());
				}
				return GetPreferredDisplay();
			}

			bool GetDesktopResolution(int index, Resolution& out) {
				SDL_DisplayMode mode;
				if (SDL_GetDesktopDisplayMode(index, &mode) != 0 || mode.w <= 0 || mode.h <= 0) {
					SPLog("Couldn't get the desktop mode of display %d: %s", index,
					      SDL_GetError());
					return false;
				}
				out.width = mode.w;
				out.height = mode.h;
				return true;
			}

			std::vector<Resolution> GetVideoModes(int index) {
				std::vector<Resolution> modes;
				std::set<std::pair<int, int>> found;

				int count = SDL_GetNumDisplayModes(index);
				for (int i = 0; i < count; i++) {
					SDL_DisplayMode mode;
					if (SDL_GetDisplayMode(index, i, &mode) != 0)
						continue;
					if (mode.w < 640 || mode.h < 480)
						continue;
					if (!found.insert(std::make_pair(mode.w, mode.h)).second)
						continue;
					modes.push_back({mode.w, mode.h});
				}

				// The desktop mode isn't always listed (e.g. scaled HiDPI modes).
				Resolution desktop;
				if (GetDesktopResolution(index, desktop) && desktop.width >= 640 &&
				    desktop.height >= 480 &&
				    found.insert(std::make_pair(desktop.width, desktop.height)).second)
					modes.push_back(desktop);

				if (modes.empty()) {
					SPLog("Failed to get the video modes of display %d. Using a stock list.",
					      index);
					modes = {{1920, 1080}, {1280, 720}, {1024, 768}, {800, 600}, {640, 480}};
				}

				std::sort(modes.begin(), modes.end(), [](const Resolution& a, const Resolution& b) {
					return a.width != b.width ? a.width > b.width : a.height > b.height;
				});
				return modes;
			}
		} // namespace display
	} // namespace gui
} // namespace spades
