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

#include "TextUtils.h"
#include <algorithm>
#include <cctype>

namespace spades {
	namespace gui {
		namespace ui {
			namespace {
				// Number of bytes in the UTF-8 sequence starting with lead byte `c`.
				int UTF8SequenceLength(unsigned char c) {
					if ((c & 0x80) == 0)
						return 1;
					else if ((c & 0xE0) == 0xC0)
						return 2;
					else if ((c & 0xF0) == 0xE0)
						return 3;
					else if ((c & 0xF8) == 0xF0)
						return 4;
					else if ((c & 0xFC) == 0xF8)
						return 5;
					else if ((c & 0xFE) == 0xFC)
						return 6;
					else
						return 1;
				}
			} // namespace

			int GetByteIndexForString(const std::string& s, int charIndex, int start) {
				int len = static_cast<int>(s.size());
				while (start < len && charIndex > 0) {
					start += UTF8SequenceLength(static_cast<unsigned char>(s[start]));
					charIndex--;
				}
				return start;
			}

			int GetCharIndexForString(const std::string& s, int byteIndex, int start) {
				int len = static_cast<int>(s.size());
				int charIndex = 0;
				while (start < len && start < byteIndex && byteIndex > 0) {
					start += UTF8SequenceLength(static_cast<unsigned char>(s[start]));
					charIndex++;
				}
				return charIndex;
			}

			size_t StringCommonPrefixLength(const std::string& a, const std::string& b) {
				size_t ln = std::min(a.size(), b.size());
				for (size_t i = 0; i < ln; i++) {
					if (std::tolower(static_cast<unsigned char>(a[i])) !=
					    std::tolower(static_cast<unsigned char>(b[i])))
						return i;
				}
				return ln;
			}

			std::string FormatFileSize(std::int64_t bytes) {
				if (bytes < 0)
					return "";
				if (bytes < 1024)
					return std::to_string(bytes) + " B";
				if (bytes < 1024 * 1024)
					return std::to_string(bytes / 1024) + " KB";
				return std::to_string(bytes / (1024 * 1024)) + " MB";
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
