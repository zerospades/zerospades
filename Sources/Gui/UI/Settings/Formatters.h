/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

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
		/**
		 * Turns a numeric config value into a display string. Declarative: a plain
		 * numeric formatter is fully described by its fields; special-cased ones
		 * (FOV, sensitivity presets, named colours…) provide a `custom` function
		 * that may fall back to `FormatConfigNumber`.
		 */
		struct Formatter {
			int digits = 0;
			std::string suffix;
			std::string prefix;
			float prescale = 1.0F;
			std::function<std::string(float)> custom;

			std::string Format(float value) const;
		};

		/** Maps a raw slider value before it is stored (e.g. FPS < 20 -> unlimited). */
		using ValueMapper = std::function<float(float)>;

		/** The default numeric formatting used by `Formatter` and its custom cases. */
		std::string FormatConfigNumber(int digits, const std::string& prefix,
		                               const std::string& suffix, float prescale, float value);

		Formatter NumberFormatter(int digits, std::string suffix, std::string prefix = "",
		                          float prescale = 1.0F);
		Formatter ViewmodelSideFormatter();
		Formatter FOVFormatter();
		Formatter RenderScaleFormatter();
		Formatter FPSFormatter();
		Formatter SensScaleFormatter();
		Formatter HUDColorFormatter();
		Formatter TargetColorFormatter();
		Formatter ScopeTypeFormatter();
	} // namespace gui
} // namespace spades
