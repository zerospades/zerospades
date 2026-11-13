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
		 * Whether `key` is the key a binding setting (cg_keyMoveForward, ...)
		 * names, matching keys the way the game does (with its "space"
		 * aliases), so the editors honour the player's bindings.
		 */
		bool EditorKeyMatches(const std::string& binding, const std::string& key);

		/**
		 * Whether the Meta key is the one commands' shortcuts are held with:
		 * on macOS, where it is Cmd and Cmd+S is how anything is saved. Ctrl
		 * is that key everywhere, macOS included, so nothing learnt on one
		 * system stops working on another.
		 */
		bool MetaIsCommandModifier();
		/** A command's shortcut as the system's users know it: "Cmd+S" on
		 *  macOS, "Ctrl+S" elsewhere, for `keys` "S". */
		std::string CommandShortcut(const std::string& keys);
	} // namespace gui
} // namespace spades
