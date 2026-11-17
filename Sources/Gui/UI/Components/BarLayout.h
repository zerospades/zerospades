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

namespace spades {
	namespace gui {
		/**
		 * Where an editor's bars stand and how their buttons are spaced: the
		 * ribbon across the top of the window, the toolbar under it, and a band
		 * for each row of the option bar under that. One place says it, for the
		 * bars, which draw and hit-test by it, and for the editor, which starts
		 * its view under them.
		 */
		namespace barlayout {
			constexpr float kRibbonH = 24.0F;
			constexpr float kToolbarH = 32.0F;
			constexpr float kSubBarH = 28.0F;
			// A button on a bar, the gap to the next of its group, the room a
			// separator takes between two groups, and the bars' left margin.
			constexpr float kTbH = 24.0F;
			constexpr float kTbGap = 2.0F;
			constexpr float kTbSep = 14.0F;
			constexpr float kTbX0 = 12.0F;
		} // namespace barlayout
	} // namespace gui
} // namespace spades
