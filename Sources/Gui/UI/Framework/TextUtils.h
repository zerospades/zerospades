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

#include <cstdint>
#include <string>
#include <vector>

namespace spades {
	namespace client {
		class IFont;
	}
	namespace gui {
		namespace ui {
			/** Which end of an over-long text is replaced by "..". */
			enum class TextElision {
				End,  // "/Users/name/Libr.."  (keeps the beginning)
				Start // "..Resources/kv6/gun" (keeps the end, e.g. paths)
			};

			/**
			 * Converts a UTF-8 code-point count into a byte offset.
			 *
			 * Advances `charIndex` code points from byte offset `start` and returns the
			 * resulting byte offset, clamped to the end of the string.
			 */
			int GetByteIndexForString(const std::string& s, int charIndex, int start = 0);

			/**
			 * Converts a byte offset into a UTF-8 code-point count.
			 *
			 * Returns the number of whole code points lying before byte offset
			 * `byteIndex`, starting the scan at byte offset `start`.
			 */
			int GetCharIndexForString(const std::string& s, int byteIndex, int start = 0);

			size_t StringCommonPrefixLength(const std::string& a, const std::string& b);
			std::string FormatFileSize(std::int64_t bytes);

			/**
			 * Splits `text` into rows that are at most `maxWidth` wide. A row breaks at its last
			 * space when it has one, otherwise between two characters, so a URL or a path is
			 * split instead of cut. Always returns at least one row.
			 */
			std::vector<std::string> WrapText(client::IFont& font, const std::string& text,
			                                  float maxWidth);

			/**
			 * Wraps `text` like `WrapText`, but returns the byte offset at which each row starts
			 * instead of copies of the rows, so a position in `text` can be mapped to its row.
			 * Row `i` spans [result[i], result[i + 1]), the last one ends at `text.size()`. No byte
			 * is dropped: the space where a row breaks stays at the end of its row. Always returns
			 * at least one offset (0).
			 */
			std::vector<int> WrapTextOffsets(client::IFont& font, const std::string& text,
			                                 float maxWidth);

			/**
			 * Returns `text` unchanged if it is at most `maxWidth` pixels wide when
			 * drawn with `font` at `scale`; otherwise the longest version with one
			 * end replaced by ".." that fits (never splitting a UTF-8 sequence).
			 * Returns an empty string if not even ".." fits.
			 */
			std::string ElideText(client::IFont& font, const std::string& text, float maxWidth,
			                      float scale = 1.0F, TextElision elision = TextElision::End);
		} // namespace ui
	} // namespace gui
} // namespace spades
