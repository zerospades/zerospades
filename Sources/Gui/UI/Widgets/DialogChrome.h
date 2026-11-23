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

#include <Core/Math.h>

namespace spades {
	namespace gui {
		namespace ui {
			class UIElement;
		}

		/**
		 * The frame the game's modal dialogs share: the screen behind them
		 * dimmed, a full-width band across the middle of it, and a hairline
		 * along each of the band's edges.
		 *
		 * A dialog is sized to the whole screen and says how much room its own
		 * widgets need; it is told where to put them. Every dialog then sits in
		 * the same place, whatever it holds, and one change here moves them all.
		 */
		class DialogChrome {
		public:
			static constexpr float kButtonWidth = 150.0F;
			static constexpr float kButtonHeight = 30.0F;
			static constexpr float kButtonGap = 10.0F;

			/**
			 * Adds the dim (unless `dim` is false) and the band to `dialog`, and
			 * returns the centred rectangle its contents go in. `contentsWidth` is
			 * capped to what the screen can hold.
			 */
			static AABB2 Attach(ui::UIElement& dialog, float contentsWidth, float contentsHeight,
			                    bool dim = true);

			/** The band's hairlines; call from the dialog's own `Render`. */
			static void DrawEdges(const ui::UIElement& dialog, const AABB2& contents);

			/**
			 * A button in the row along the bottom of `contents`, `slot` places
			 * from the right (0 is the rightmost).
			 */
			static AABB2 ButtonSlot(const AABB2& contents, int slot);
		};
	} // namespace gui
} // namespace spades
