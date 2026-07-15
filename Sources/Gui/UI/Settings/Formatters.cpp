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

#include "Formatters.h"
#include <Core/Strings.h>

namespace spades {
	namespace gui {
		namespace {
			std::string FormatInternal(int digits, const std::string& prefix,
			                           const std::string& suffix, float prescale, float value) {
				if (value < 0.0F)
					return "-" + FormatConfigNumber(digits, prefix, suffix, prescale, -value);

				value *= prescale;

				// do rounding
				float rounding = 0.5F;
				for (int i = digits; i > 0; i--)
					rounding *= 0.1F;
				value += rounding;

				int intPart = static_cast<int>(value);
				std::string s = std::to_string(intPart);
				if (digits > 0) {
					s += ".";
					for (int i = digits; i > 0; i--) {
						value -= static_cast<float>(intPart);
						value *= 10.0F;
						intPart = static_cast<int>(value);
						if (intPart > 9)
							intPart = 9;
						s += std::to_string(intPart);
					}
				}
				s += suffix;
				return s;
			}
		} // namespace

		std::string FormatConfigNumber(int digits, const std::string& prefix,
		                               const std::string& suffix, float prescale, float value) {
			return prefix + FormatInternal(digits, prefix, suffix, prescale, value);
		}

		std::string Formatter::Format(float value) const {
			if (custom)
				return custom(value);
			return FormatConfigNumber(digits, prefix, suffix, prescale, value);
		}

		Formatter NumberFormatter(int digits, std::string suffix, std::string prefix,
		                          float prescale) {
			Formatter f;
			f.digits = digits;
			f.suffix = std::move(suffix);
			f.prefix = std::move(prefix);
			f.prescale = prescale;
			return f;
		}

		Formatter ViewmodelSideFormatter() {
			Formatter f;
			f.digits = 1;
			f.custom = [](float value) -> std::string {
				if (value == -1)
					return _Tr("Preferences", "Left");
				else if (value == 0)
					return _Tr("Preferences", "Center");
				else if (value == 1)
					return _Tr("Preferences", "Right");
				else
					return FormatConfigNumber(1, "", "", 1.0F, value);
			};
			return f;
		}

		Formatter FOVFormatter() {
			Formatter f;
			f.custom = [](float value) -> std::string {
				if (value == 68)
					return _Tr("Preferences", "Default");
				else if (value >= 110)
					return _Tr("Preferences", "Quake Pro");
				else
					return FormatConfigNumber(0, "", "", 1.0F, value);
			};
			return f;
		}

		Formatter RenderScaleFormatter() {
			Formatter f;
			f.custom = [](float value) -> std::string {
				if (value == 1)
					return _Tr("Preferences", "Bicubic");
				else if (value == 2)
					return _Tr("Preferences", "Bilinear");
				else
					return _Tr("Preferences", "Nearest Neighbour");
			};
			return f;
		}

		Formatter FPSFormatter() {
			Formatter f;
			f.suffix = "FPS";
			f.custom = [](float value) -> std::string {
				if (value == 0)
					return _Tr("Preferences", "Unlimited");
				else
					return FormatConfigNumber(0, "", "FPS", 1.0F, value);
			};
			return f;
		}

		Formatter SensScaleFormatter() {
			Formatter f;
			f.custom = [](float value) -> std::string {
				if (value == 1)
					return "Voxlap";
				else if (value == 2)
					return "BetterSpades";
				else if (value == 3)
					return "Quake/Source";
				else if (value == 4)
					return "Overwatch";
				else if (value == 5)
					return "Valorant";
				else
					return _Tr("Preferences", "Default");
			};
			return f;
		}

		Formatter HUDColorFormatter() {
			Formatter f;
			f.custom = [](float value) -> std::string {
				if (value == 1)
					return _Tr("Preferences", "Team Color");
				else if (value == 2)
					return _Tr("Preferences", "Light Blue");
				else if (value == 3)
					return _Tr("Preferences", "Blue");
				else if (value == 4)
					return _Tr("Preferences", "Purple");
				else if (value == 5)
					return _Tr("Preferences", "Red");
				else if (value == 6)
					return _Tr("Preferences", "Orange");
				else if (value == 7)
					return _Tr("Preferences", "Yellow");
				else if (value == 8)
					return _Tr("Preferences", "Green");
				else if (value == 9)
					return _Tr("Preferences", "Aqua");
				else if (value == 10)
					return _Tr("Preferences", "Pink");
				else
					return _Tr("Preferences", "Custom");
			};
			return f;
		}

		Formatter TargetColorFormatter() {
			Formatter f;
			f.custom = [](float value) -> std::string {
				if (value == 1)
					return _Tr("Preferences", "Red");
				else if (value == 2)
					return _Tr("Preferences", "Green");
				else if (value == 3)
					return _Tr("Preferences", "Blue");
				else if (value == 4)
					return _Tr("Preferences", "Yellow");
				else if (value == 5)
					return _Tr("Preferences", "Cyan");
				else if (value == 6)
					return _Tr("Preferences", "Pink");
				else
					return _Tr("Preferences", "Custom");
			};
			return f;
		}

		Formatter ScopeTypeFormatter() {
			Formatter f;
			f.custom = [](float value) -> std::string {
				if (value == 1)
					return _Tr("Preferences", "Classic");
				else if (value == 2)
					return _Tr("Preferences", "Dot Sight");
				else if (value == 3)
					return _Tr("Preferences", "Custom");
				else
					return _Tr("Preferences", "Iron Sight");
			};
			return f;
		}
	} // namespace gui
} // namespace spades
