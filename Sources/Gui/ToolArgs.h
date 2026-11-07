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

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace spades {
	/**
	 * What the command-line tools (--kv6-tools, --2kv6-tools) share: reading
	 * their arguments, colours as RRGGBB, and how a mistake is reported.
	 */
	namespace toolargs {
		/** What went wrong, without the call stack the engine's errors carry. */
		inline std::string ErrorText(const std::exception& ex) {
			std::string text = ex.what();
			const std::size_t end = text.find('\n');
			return end == std::string::npos ? text : text.substr(0, end);
		}

		/** A command the user got wrong: said with the usage, exit code 2. */
		struct UsageError : std::runtime_error {
			using std::runtime_error::runtime_error;
		};

		inline int ParseInt(const std::string& text, const char* what) {
			char* end = nullptr;
			errno = 0;
			const long value = std::strtol(text.c_str(), &end, 10);
			if (text.empty() || *end != '\0' || errno != 0 || value < -1000000 || value > 1000000)
				throw UsageError(std::string("not a whole number for ") + what + ": '" + text + "'");
			return int(value);
		}

		inline float ParseFloat(const std::string& text, const char* what) {
			char* end = nullptr;
			const float value = std::strtof(text.c_str(), &end);
			if (text.empty() || *end != '\0')
				throw UsageError(std::string("not a number for ") + what + ": '" + text + "'");
			return value;
		}

		inline std::uint32_t ParseColor(std::string text) {
			if (!text.empty() && text[0] == '#')
				text = text.substr(1);
			if (text.size() == 8 && (text.compare(0, 2, "0x") == 0 || text.compare(0, 2, "0X") == 0))
				text = text.substr(2);
			char* end = nullptr;
			const unsigned long rgb = std::strtoul(text.c_str(), &end, 16);
			if (text.size() != 6 || *end != '\0')
				throw UsageError("not a colour (RRGGBB in hex): '" + text + "'");
			const std::uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
			return r | (g << 8) | (b << 16); // packed 0x00BBGGRR
		}

		inline std::string FormatColor(std::uint32_t packed) {
			char text[8];
			std::snprintf(text, sizeof(text), "%02x%02x%02x", packed & 0xFF, (packed >> 8) & 0xFF,
			              (packed >> 16) & 0xFF);
			return text;
		}

		inline std::string FormatFloat(float v) {
			char text[32];
			std::snprintf(text, sizeof(text), "%g", double(v));
			return text;
		}

		inline int ParseAxis(const std::string& v) {
			if (v == "x" || v == "X")
				return 0;
			if (v == "y" || v == "Y")
				return 1;
			if (v == "z" || v == "Z")
				return 2;
			throw UsageError("an axis is x, y or z, not '" + v + "'");
		}

		inline void Need(const std::vector<std::string>& a, std::size_t count, const char* usage) {
			if (a.size() != count)
				throw UsageError(std::string("usage: ") + usage);
		}

		inline std::vector<std::string> SplitWords(const std::string& line) {
			std::vector<std::string> words;
			std::istringstream in(line);
			std::string word;
			while (in >> word)
				words.push_back(word);
			return words;
		}
	} // namespace toolargs
} // namespace spades
