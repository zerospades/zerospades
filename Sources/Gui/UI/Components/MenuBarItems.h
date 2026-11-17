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

#include <functional>
#include <string>
#include <vector>

namespace spades {
	namespace gui {
		/** One row of a menu of an editor's menu bar, supplied by the host. */
		struct MenuBarItem {
			std::string caption;
			std::function<void()> run;
			/** False draws the row greyed out and ignores clicks on it. */
			bool enabled = true;
			/** The key that does the same, shown on the row's right: a menu is
             *  where a key is learned. Empty for a command with no key. */
			std::string hotKey;
			/** A rule between two blocks of commands rather than a command: it
             *  is not picked, clicked or stepped onto. */
			bool separator = false;
			/** Ticked: a command that turns something on shows whether it is. */
			bool checked = false;
		};

		/** A command, with the key that does the same where it has one. */
		inline MenuBarItem MenuCommand(std::string caption, std::function<void()> run,
                                          bool enabled = true,
                                          std::string hotKey = std::string()) {
			MenuBarItem item;
			item.caption = std::move(caption);
			item.run = std::move(run);
			item.enabled = enabled;
			item.hotKey = std::move(hotKey);
			return item;
		}

		/** A rule between two blocks of commands. */
		inline MenuBarItem MenuRule() {
			MenuBarItem item;
			item.separator = true;
			return item;
		}

		/** One menu of a menu bar: its title and what it offers. */
		struct MenuBarGroup {
			std::string title;
			std::vector<MenuBarItem> items;
		};

		/** Whoever supplies a menu bar its menus: the editor view. */
		class IMenuBarHost {
		public:
			virtual ~IMenuBarHost() = default;
			/**
             * The menus of the editor's menu bar, rebuilt whenever one is
             * opened so that what it offers, what is greyed out and what is
             * ticked are as they stand at that moment.
             */
			virtual std::vector<MenuBarGroup> GetMenus() = 0;
		};

	} // namespace gui
} // namespace spades
