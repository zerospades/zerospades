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

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace spades {
	namespace gui {
		/**
		 * One line of text being typed, for controls drawn without the widget
		 * framework (an immediate-mode bar, a row of a tree), which have no
		 * retained widget to take the keyboard.
		 *
		 * It holds the text, the caret and the selection (between the caret and
		 * its anchor), all as byte indices that always fall between two UTF-8
		 * characters, and an undo history of its own. It draws nothing and
		 * knows no layout: whoever shows it places it, paints it, and tells it
		 * what the user did, through `Key` for the editing keys as each
		 * platform has them, and `Insert` for what was typed.
		 */
		class TextEntry {
		public:
			struct Modifiers {
				bool shift = false;
				bool ctrl = false;
				bool alt = false;  // Option on macOS
				bool meta = false; // Cmd on macOS
			};

			/** Text longer than `maxBytes` is never made. */
			explicit TextEntry(std::size_t maxBytes);

			/** Starts editing `text`, all of it selected or the caret at its end. */
			void Begin(const std::string& text, bool selectAll);
			void End();
			bool IsEditing() const { return editing; }

			const std::string& Text() const { return text; }
			int Caret() const { return caret; }
			int Anchor() const { return anchor; }
			bool HasSelection() const { return caret != anchor; }
			int SelectionStart() const { return caret < anchor ? caret : anchor; }
			int SelectionEnd() const { return caret < anchor ? anchor : caret; }
			std::string SelectedText() const;
			/** Bumped by every change of the text, the caret or the selection,
			 *  for a caret that shows steadily while it is being moved. */
			unsigned long Revision() const { return revision; }

			/**
			 * Puts `s` in place of the selection, leaving out what cannot be
			 * typed into one line (control characters; a line break becomes a
			 * space). False, nothing changed, when the text would grow too long.
			 */
			bool Insert(const std::string& s);
			/** Erases the selection, or the character (or word) before the caret. */
			void Backspace(bool word);
			/** Erases the selection, or the character (or word) after the caret. */
			void Delete(bool word);
			/** Moves the caret by characters; without `extend`, a selection
			 *  collapses to its side in that direction first. */
			void Move(int characters, bool extend);
			/** To the start of this or the last word, or the end of this or the
			 *  next one. */
			void MoveWord(int direction, bool extend);
			void ToStart(bool extend);
			void ToEnd(bool extend);
			/** Puts the caret at the character boundary nearest `byteIndex`. */
			void SetCaret(int byteIndex, bool extend);
			void SelectAll();
			/** Selects the word (or the run of what is not one) at `byteIndex`. */
			void SelectWordAt(int byteIndex);

			bool Undo();
			bool Redo();

			/**
			 * The editing keys as the platform has them: arrows, Home and End,
			 * Backspace and Delete, by word with Ctrl (Option on macOS), Cmd+arrows
			 * on macOS, and Ctrl (Cmd) with A, C, X, V, Z and Y. True when `key`
			 * was one of them; the text itself comes through Insert.
			 */
			bool Key(const std::string& key, const Modifiers& modifiers);
			/** Whether typed text should reach Insert with these held: not while
			 *  a shortcut's modifier is, which types nothing. */
			static bool Types(const Modifiers& modifiers);

			/** The boundary nearest `x`, with the text from byte `from` drawn at
			 *  0 and `width` measuring a piece of it. */
			int IndexAt(float x, int from, const std::function<float(const std::string&)>& width) const;
			/** The next or previous character boundary from `index`. */
			int Next(int index) const;
			int Previous(int index) const;

		private:
			struct State {
				std::string text;
				int caret, anchor;
			};
			enum class Edit { Typing, Erasing, Other };

			std::size_t maxBytes;
			bool editing = false;
			std::string text;
			int caret = 0, anchor = 0;
			unsigned long revision = 0;
			std::vector<State> undo, redo;
			Edit lastEdit = Edit::Other; // a run of typing undoes as one

			/** Keeps the state before an edit of `kind`, unless it goes on a
			 *  run of the same. */
			void Remember(Edit kind);
			void Changed() { revision++; }
			void EraseSelection();
			int WordStart(int index) const;
			int WordEnd(int index) const;
		};
	} // namespace gui
} // namespace spades
