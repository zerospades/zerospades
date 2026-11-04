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

#include <Gui/UI/Widgets/ButtonBase.h>

namespace spades {
	namespace gui {
		/**
		 * A clickable strip announcing a newer official mods release, for players who
		 * installed the official mods.
		 *
		 * It follows `OfficialModsChecker` on its own and draws nothing until there is
		 * something to announce:
		 *   - a newer release this build can use: clicking runs `downloadRequested`;
		 *   - only a release that needs a newer ZeroSpades: clicking opens the game's
		 *     releases page, so the player can update the game first.
		 *
		 * It gives way to `UpdateNotice`: while a game update is announced, that
		 * notice already asks for the update, so this one stays hidden.
		 */
		class OfficialModsNotice : public ui::ButtonBase {
			std::function<void()> downloadRequested;
			bool needsGameUpdate = false;

			void Refresh();

		public:
			OfficialModsNotice(ui::UIManager* manager, std::function<void()> downloadRequested);

			/** Whether it has something to announce now, and so draws. */
			bool IsShown();

			void OnActivated() override;
			void Render() override;
		};
	} // namespace gui
} // namespace spades
