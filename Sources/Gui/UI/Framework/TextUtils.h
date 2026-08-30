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

#include <string>

namespace spades {
	namespace gui {
		namespace ui {
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

			/**
			 * Returns the length of the common prefix between two strings (case-insensitive).
			 */
			size_t StringCommonPrefixLength(const std::string& a, const std::string& b);

			/**
			 * Formats a file size in bytes as a human-readable string.
			 */
			std::string FormatFileSize(int64_t bytes);
		} // namespace ui
	} // namespace gui
} // namespace spades
