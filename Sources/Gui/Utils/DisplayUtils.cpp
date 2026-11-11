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
#include <Core/Strings.h>
#include <Imports/SDL.h>

DEFINE_SPADES_SETTING(r_videoDisplay, "0");
DEFINE_SPADES_SETTING(r_videoDisplayName, "");
// Use the desktop resolution of the monitor the game opens on.
DEFINE_SPADES_SETTING(r_videoAutoResolution, "1");
// Set once r_videoAutoResolution has been defaulted for this config.
DEFINE_SPADES_SETTING(r_videoAutoResolutionChecked, "0");
SPADES_SETTING(r_videoWidth);
SPADES_SETTING(r_videoHeight);

namespace spades {
	namespace gui {
		namespace display {
			namespace {
				/**
				 * The monitor's own name, or "" if it has none. When a backend can't
				 * name a monitor SDL substitutes its index ("0", "1", ...), which
				 * neither identifies it across replugs nor means anything to a person.
				 */
				std::string RealDisplayName(int index) {
					const char* name = SDL_GetDisplayName(index);
					if (!name || !*name)
						return std::string();
					std::string str = name;
					if (std::all_of(str.begin(), str.end(),
					                [](char c) { return c >= '0' && c <= '9'; }))
						return std::string();
					return str;
				}
			} // namespace

			int GetNumDisplays() { return std::max(SDL_GetNumVideoDisplays(), 0); }

			std::string GetDisplayName(int index) {
				std::string name = RealDisplayName(index);
				if (!name.empty())
					return name;
				return _Tr("StartupScreen", "Display {0}", index + 1);
			}

			int GetPreferredDisplay() {
				int count = GetNumDisplays();
				if (count <= 0)
					return 0;

				int index = r_videoDisplay;
				bool indexValid = index >= 0 && index < count;
				std::string name = r_videoDisplayName;
				if (name.empty()) // the backend names no monitor: the index is all we have
					return indexValid ? index : 0;

				// Still where we left it. Checked first, so two identical monitors
				// (same name) resolve to the one that was chosen, not the first.
				if (indexValid && RealDisplayName(index) == name)
					return index;

				// Monitors were plugged in or out and the indices shifted: follow the
				// name. Empty ones can't match a name that isn't.
				bool anyNamed = false;
				for (int i = 0; i < count; i++) {
					std::string other = RealDisplayName(i);
					if (other.empty())
						continue;
					anyNamed = true;
					if (other == name)
						return i;
				}

				// No monitor is named at all, though one was when the choice was saved:
				// this backend (another driver, X11 against Wayland, an older SDL) can't
				// name them, so the saved index is all there is to go on.
				if (!anyNamed)
					return indexValid ? index : 0;

				// The monitor is gone. Its old index may now be a different monitor, so
				// use the primary one.
				return 0;
			}

			void SetPreferredDisplay(int index) {
				if (index < 0 || index >= GetNumDisplays())
					return;
				r_videoDisplay = index;
				r_videoDisplayName = RealDisplayName(index);
			}

			int GetWindowDisplay(SDL_Window* window) {
				if (window) {
					// SDL picks the display holding the window's centre (the closest one
					// if the centre is off-screen), so a window straddling two monitors
					// resolves to the one showing most of it.
					int index = SDL_GetWindowDisplayIndex(window);
					if (index >= 0 && index < GetNumDisplays())
						return index;
					// Polled while the setup screen is up: say it once, not every time.
					static bool logged = false;
					if (!logged) {
						logged = true;
						SPLog("Couldn't tell which display the window is on: %s", SDL_GetError());
					}
				}
				return GetPreferredDisplay();
			}

			bool IsAutoResolution() {
				// Configs from before auto-detection: keep a resolution someone chose.
				// The stock 800x600 and the monitor's own native size are taken as
				// "never chosen", so those switch to auto-detection.
				if (!r_videoAutoResolutionChecked) {
					r_videoAutoResolutionChecked = 1;
					int w = r_videoWidth, h = r_videoHeight;
					Resolution native;
					bool isNative = GetDesktopResolution(GetPreferredDisplay(), native) &&
					                native.width == w && native.height == h;
					r_videoAutoResolution = (isNative || (w == 800 && h == 600)) ? 1 : 0;
				}
				return r_videoAutoResolution;
			}

			void SetAutoResolution(bool on) {
				r_videoAutoResolutionChecked = 1;
				r_videoAutoResolution = on ? 1 : 0;
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

			bool GetUsableBounds(int index, Rect& out) {
				SDL_Rect r;
#if SDL_VERSION_ATLEAST(2, 0, 5)
				if (SDL_GetDisplayUsableBounds(index, &r) == 0 && r.w > 0 && r.h > 0) {
					out = {r.x, r.y, r.w, r.h};
					return true;
				}
#endif
				if (SDL_GetDisplayBounds(index, &r) == 0 && r.w > 0 && r.h > 0) {
					out = {r.x, r.y, r.w, r.h};
					return true;
				}
				return false;
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
