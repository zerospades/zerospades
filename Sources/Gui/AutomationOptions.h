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
#include <utility>
#include <vector>

namespace spades {
	namespace gui {
		/**
		 * Options for running the program unattended, to check what it draws
		 * without anyone at the keyboard: set from the command line, read where
		 * they apply.
		 */
		struct AutomationOptions {
			/** Seconds after start to take a screenshot (into Screenshots/) and
			 *  quit; negative for never (--screenshot-quit). */
			float screenshotQuitAfter = -1.0F;
			/** Reference images the model editor opened with --open shows at once,
			 *  each across an axis (0 = x, 1 = y, 2 = z) (--editor-reference). */
			std::vector<std::pair<int, std::string>> editorReferences;
		};

		AutomationOptions& Automation();
	} // namespace gui
} // namespace spades
