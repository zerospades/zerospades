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

#include <Client/IFont.h>

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

			std::vector<std::string> WrapText(client::IFont& font, const std::string& text,
			                                  float maxWidth) {
				const std::vector<int> starts = WrapTextOffsets(font, text, maxWidth);
				std::vector<std::string> rows;
				rows.reserve(starts.size());
				for (size_t i = 0; i < starts.size(); i++) {
					const bool last = i + 1 == starts.size();
					const int begin = starts[i];
					int end = last ? static_cast<int>(text.size()) : starts[i + 1];

					// The offsets keep the space a row breaks at, at the end of its row. The rows
					// are returned without it. The last row only has one when it did not fit.
					if (end - begin > 1 && text[end - 1] == ' ' &&
					    (!last || font.Measure(text.substr(begin, end - begin)).x > maxWidth))
						end--;

					rows.push_back(text.substr(begin, end - begin));
				}
				return rows;
			}

			std::vector<int> WrapTextOffsets(client::IFont& font, const std::string& text,
			                                 float maxWidth) {
				std::vector<int> starts(1, 0);
				if (font.Measure(text).x <= maxWidth)
					return starts;

				const int length = static_cast<int>(text.size());
				int rowStart = 0;
				// offset right after the last space of the current row, `rowStart` when it has none
				int lastBreak = 0;
				int i = 0;
				while (i < length) {
					// one whole UTF-8 character
					int len = 1;
					while (i + len < length &&
					       (static_cast<unsigned char>(text[i + len]) & 0xC0) == 0x80)
						len++;

					// A row always keeps at least one character, so a character wider than
					// `maxWidth` cannot loop forever.
					if (i > rowStart &&
					    font.Measure(text.substr(rowStart, i + len - rowStart)).x > maxWidth) {
						if (text[i] == ' ') {
							// a space that does not fit stays at the end of its row, so the next row
							// does not start with it
							i += len;
							if (i < length) {
								rowStart = i;
								starts.push_back(rowStart);
								lastBreak = rowStart;
							}
							continue;
						}

						// the row is full: break after its last space so the word stays whole,
						// otherwise split the word here
						if (lastBreak > rowStart + 1)
							rowStart = lastBreak;
						else
							rowStart = i;
						starts.push_back(rowStart);
						lastBreak = rowStart;
						// `i` is handled again, now against the new row
						continue;
					}

					if (text[i] == ' ')
						lastBreak = i + len;
					i += len;
				}
				return starts;
			}

			std::string ElideText(client::IFont& font, const std::string& text, float maxWidth,
			                      float scale, TextElision elision) {
				auto fits = [&](const std::string& s) { return font.Measure(s).x * scale <= maxWidth; };
				if (fits(text))
					return text;

				const std::string dots = "..";
				int len = static_cast<int>(text.size());
				int chars = GetCharIndexForString(text, len);
				// The candidate keeping `k` characters of the original text.
				auto candidate = [&](int k) {
					if (elision == TextElision::End)
						return text.substr(0, std::min(len, GetByteIndexForString(text, k))) + dots;
					int from = std::min(len, GetByteIndexForString(text, chars - k));
					return dots + text.substr(from);
				};

				// Width grows with `k`, so binary-search the largest `k` that fits.
				if (!fits(candidate(0)))
					return std::string();
				int lo = 0, hi = chars;
				while (lo < hi) {
					int mid = (lo + hi + 1) / 2;
					if (fits(candidate(mid)))
						lo = mid;
					else
						hi = mid - 1;
				}
				return candidate(lo);
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
