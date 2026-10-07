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

namespace spades {
	namespace gui {
		/**
		 * A number being typed, and how it is written down.
		 *
		 * `Format` and `Parse` are the pair every numeric control in the editor
		 * reads and writes with, so a value shown in one box is the same text in
		 * the next.
		 *
		 * The rest is the state of an edit in progress: the text as it stands and
		 * where the caret is. It holds no layout and draws nothing — whoever
		 * shows one places it, paints it and tells it what the user did. This is
		 * what lets an immediate-mode bar, which has no retained widgets to take
		 * keyboard focus, still be typed into.
		 *
		 * Editing is on the text, not on the value: what is typed stays exactly
		 * as typed until it is read, so a half-finished "-" or "1." is never
		 * corrected under the caret or mistaken for a number.
		 */
		class NumberEntry {
		public:
			/** Writes `value` with `decimals` places. */
			static std::string Format(float value, int decimals);
			/**
			 * Reads a number filling the whole of `text`, commas allowed for
			 * decimal-comma keyboards. False when it is anything else, including
			 * empty, half typed ("-", "1.e") or not finite.
			 */
			static bool Parse(const std::string& text, float& out);

			/**
			 * Starts editing `value`. The caret sits at the end and the first
			 * character typed replaces the whole of it, the way focusing a field
			 * that selected its text would.
			 */
			void Begin(float value, int decimals);
			void End();
			bool IsEditing() const { return editing; }

			const std::string& Text() const { return text; }
			/** Byte index of the caret within `Text()`. */
			int Caret() const { return caret; }

			/** Takes the digits, signs and separators in `s`; ignores the rest. */
			void Insert(const std::string& s);
			void Backspace();
			void Delete();
			/** Moves the caret `by` characters, stopping at either end. */
			void MoveCaret(int by);
			void CaretToStart();
			void CaretToEnd();

			/** The number as typed; false while the text is not one. */
			bool Value(float& out) const;

		private:
			bool editing = false;
			// True until the first character is typed, which then replaces the
			// text rather than adding to it.
			bool fresh = false;
			std::string text;
			int caret = 0;
		};
	} // namespace gui
} // namespace spades
