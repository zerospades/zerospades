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

#include "NumberEntry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace spades {
	namespace gui {
		namespace {
			// Long enough for a signed number with a few decimals, short enough
			// that nothing pathological is ever parsed or drawn.
			const int kMaxLength = 16;

			bool IsNumberChar(char c) {
				return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == ',';
			}
		} // namespace

		std::string NumberEntry::Format(float value, int decimals) {
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%.*f", std::max(0, decimals), value);
			return buf;
		}

		bool NumberEntry::Parse(const std::string& text, float& out) {
			std::string str = text;
			for (char& c : str)
				if (c == ',')
					c = '.';
			const char* begin = str.c_str();
			char* end = nullptr;
			float parsed = std::strtof(begin, &end);
			if (end == begin || !std::isfinite(parsed))
				return false;
			while (*end == ' ' || *end == '\t')
				end++;
			if (*end != '\0')
				return false;
			out = parsed;
			return true;
		}

		void NumberEntry::Begin(float value, int decimals) {
			editing = true;
			fresh = true;
			text = Format(value, decimals);
			caret = int(text.size());
		}

		void NumberEntry::End() {
			editing = false;
			fresh = false;
			text.clear();
			caret = 0;
		}

		void NumberEntry::Insert(const std::string& s) {
			if (!editing)
				return;
			std::string accepted;
			for (char c : s)
				if (IsNumberChar(c))
					accepted += c;
			if (accepted.empty())
				return;
			// The first thing typed stands in for the value that was there, as
			// typing into a field that selected its text on focus would.
			if (fresh) {
				text.clear();
				caret = 0;
				fresh = false;
			}
			if (int(text.size()) + int(accepted.size()) > kMaxLength)
				return;
			text.insert(size_t(caret), accepted);
			caret += int(accepted.size());
		}

		void NumberEntry::Backspace() {
			if (!editing)
				return;
			fresh = false;
			if (caret <= 0)
				return;
			text.erase(size_t(caret - 1), 1);
			caret--;
		}

		void NumberEntry::Delete() {
			if (!editing)
				return;
			fresh = false;
			if (caret >= int(text.size()))
				return;
			text.erase(size_t(caret), 1);
		}

		void NumberEntry::MoveCaret(int by) {
			if (!editing)
				return;
			fresh = false;
			caret = std::max(0, std::min(int(text.size()), caret + by));
		}

		void NumberEntry::CaretToStart() {
			if (!editing)
				return;
			fresh = false;
			caret = 0;
		}

		void NumberEntry::CaretToEnd() {
			if (!editing)
				return;
			fresh = false;
			caret = int(text.size());
		}

		bool NumberEntry::Value(float& out) const { return editing && Parse(text, out); }
	} // namespace gui
} // namespace spades
