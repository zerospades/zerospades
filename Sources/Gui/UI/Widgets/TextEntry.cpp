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

#include "TextEntry.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include <Core/Math.h>
#include <Imports/SDL.h>

namespace spades {
	namespace gui {
		namespace {
			// Edits kept to undo: plenty for a name, bounded for a long session.
			constexpr std::size_t kUndoDepth = 200;

#ifdef __APPLE__
			constexpr bool kMac = true;
#else
			constexpr bool kMac = false;
#endif

			bool IsContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

			// Letters, digits and '_' make words, and so does anything beyond
			// ASCII, so a word in any script is moved over whole.
			bool IsWordByte(char c) {
				const unsigned char u = static_cast<unsigned char>(c);
				return u >= 0x80 || std::isalnum(u) || c == '_';
			}
		} // namespace

		TextEntry::TextEntry(std::size_t maxBytes) : maxBytes(maxBytes) {}

		void TextEntry::Begin(const std::string& initial, bool selectAll) {
			editing = true;
			text = initial.size() <= maxBytes ? initial : std::string();
			caret = int(text.size());
			anchor = selectAll ? 0 : caret;
			undo.clear();
			redo.clear();
			lastEdit = Edit::Other;
			Changed();
		}

		void TextEntry::End() {
			editing = false;
			text.clear();
			caret = anchor = 0;
			undo.clear();
			redo.clear();
			Changed();
		}

		std::string TextEntry::SelectedText() const {
			return text.substr(std::size_t(SelectionStart()),
			                   std::size_t(SelectionEnd() - SelectionStart()));
		}

		int TextEntry::Next(int index) const {
			const int size = int(text.size());
			if (index >= size)
				return size;
			index++;
			while (index < size && IsContinuation(text[std::size_t(index)]))
				index++;
			return index;
		}

		int TextEntry::Previous(int index) const {
			if (index <= 0)
				return 0;
			index = std::min(index, int(text.size())) - 1;
			while (index > 0 && IsContinuation(text[std::size_t(index)]))
				index--;
			return index;
		}

		int TextEntry::WordStart(int index) const {
			int i = index;
			while (i > 0 && !IsWordByte(text[std::size_t(Previous(i))]))
				i = Previous(i);
			while (i > 0 && IsWordByte(text[std::size_t(Previous(i))]))
				i = Previous(i);
			return i;
		}

		int TextEntry::WordEnd(int index) const {
			const int size = int(text.size());
			int i = index;
			while (i < size && !IsWordByte(text[std::size_t(i)]))
				i = Next(i);
			while (i < size && IsWordByte(text[std::size_t(i)]))
				i = Next(i);
			return i;
		}

		void TextEntry::Remember(Edit kind) {
			if (kind != Edit::Other && kind == lastEdit)
				return; // the run goes on, and undoes as one
			undo.push_back(State{text, caret, anchor});
			if (undo.size() > kUndoDepth)
				undo.erase(undo.begin());
			redo.clear();
			lastEdit = kind;
		}

		void TextEntry::EraseSelection() {
			const int start = SelectionStart();
			text.erase(std::size_t(start), std::size_t(SelectionEnd() - start));
			caret = anchor = start;
		}

		bool TextEntry::Insert(const std::string& s) {
			if (!editing)
				return false;
			std::string clean;
			for (char c : s) {
				const unsigned char u = static_cast<unsigned char>(c);
				if (c == '\n' || c == '\r' || c == '\t')
					clean += ' ';
				else if (u >= 0x20 && u != 0x7F)
					clean += c;
			}
			if (clean.empty())
				return false;
			// What does not fit is left out, never half a character of it.
			const std::size_t room =
			  maxBytes - (text.size() - std::size_t(SelectionEnd() - SelectionStart()));
			if (clean.size() > room) {
				std::size_t cut = room;
				while (cut > 0 && IsContinuation(clean[cut]))
					cut--;
				clean.erase(cut);
				if (clean.empty())
					return false;
			}
			Remember(Edit::Typing);
			EraseSelection();
			text.insert(std::size_t(caret), clean);
			caret = anchor = caret + int(clean.size());
			Changed();
			return true;
		}

		void TextEntry::Backspace(bool word) {
			if (!editing || (!HasSelection() && caret == 0))
				return;
			Remember(Edit::Erasing);
			if (!HasSelection())
				anchor = word ? WordStart(caret) : Previous(caret);
			EraseSelection();
			Changed();
		}

		void TextEntry::Delete(bool word) {
			if (!editing || (!HasSelection() && caret >= int(text.size())))
				return;
			Remember(Edit::Erasing);
			if (!HasSelection())
				anchor = word ? WordEnd(caret) : Next(caret);
			EraseSelection();
			Changed();
		}

		void TextEntry::Move(int characters, bool extend) {
			if (!editing)
				return;
			lastEdit = Edit::Other;
			if (!extend && HasSelection()) {
				caret = anchor = characters < 0 ? SelectionStart() : SelectionEnd();
				Changed();
				return;
			}
			for (; characters < 0; characters++)
				caret = Previous(caret);
			for (; characters > 0; characters--)
				caret = Next(caret);
			if (!extend)
				anchor = caret;
			Changed();
		}

		void TextEntry::MoveWord(int direction, bool extend) {
			if (!editing)
				return;
			lastEdit = Edit::Other;
			caret = direction < 0 ? WordStart(caret) : WordEnd(caret);
			if (!extend)
				anchor = caret;
			Changed();
		}

		void TextEntry::ToStart(bool extend) { SetCaret(0, extend); }

		void TextEntry::ToEnd(bool extend) { SetCaret(int(text.size()), extend); }

		void TextEntry::SetCaret(int byteIndex, bool extend) {
			if (!editing)
				return;
			lastEdit = Edit::Other;
			int i = std::max(0, std::min(byteIndex, int(text.size())));
			while (i > 0 && i < int(text.size()) && IsContinuation(text[std::size_t(i)]))
				i--;
			caret = i;
			if (!extend)
				anchor = caret;
			Changed();
		}

		void TextEntry::SelectAll() {
			if (!editing)
				return;
			lastEdit = Edit::Other;
			anchor = 0;
			caret = int(text.size());
			Changed();
		}

		void TextEntry::SelectWordAt(int byteIndex) {
			if (!editing || text.empty())
				return;
			lastEdit = Edit::Other;
			int at = std::max(0, std::min(byteIndex, int(text.size())));
			if (at == int(text.size()))
				at = Previous(at);
			const bool word = IsWordByte(text[std::size_t(at)]);
			int start = at, end = at;
			while (start > 0 && IsWordByte(text[std::size_t(Previous(start))]) == word)
				start = Previous(start);
			while (end < int(text.size()) && IsWordByte(text[std::size_t(end)]) == word)
				end = Next(end);
			anchor = start;
			caret = end;
			Changed();
		}

		bool TextEntry::Undo() {
			if (!editing || undo.empty())
				return false;
			redo.push_back(State{text, caret, anchor});
			const State& s = undo.back();
			text = s.text;
			caret = s.caret;
			anchor = s.anchor;
			undo.pop_back();
			lastEdit = Edit::Other;
			Changed();
			return true;
		}

		bool TextEntry::Redo() {
			if (!editing || redo.empty())
				return false;
			undo.push_back(State{text, caret, anchor});
			const State& s = redo.back();
			text = s.text;
			caret = s.caret;
			anchor = s.anchor;
			redo.pop_back();
			lastEdit = Edit::Other;
			Changed();
			return true;
		}

		bool TextEntry::Types(const Modifiers& m) {
			// Ctrl with Alt is AltGr on a European keyboard, which types.
			return !m.meta && !(m.ctrl && !m.alt);
		}

		bool TextEntry::Key(const std::string& key, const Modifiers& m) {
			if (!editing)
				return false;
			const bool command = kMac ? m.meta : m.ctrl;
			const bool word = kMac ? m.alt : m.ctrl;
			const bool line = kMac && m.meta; // Cmd with an arrow: to an end

			if (key == "Left") {
				if (line)
					ToStart(m.shift);
				else if (word)
					MoveWord(-1, m.shift);
				else
					Move(-1, m.shift);
			} else if (key == "Right") {
				if (line)
					ToEnd(m.shift);
				else if (word)
					MoveWord(1, m.shift);
				else
					Move(1, m.shift);
			} else if (key == "Home" || key == "Up") {
				ToStart(m.shift);
			} else if (key == "End" || key == "Down") {
				ToEnd(m.shift);
			} else if (key == "BackSpace") {
				if (line && !HasSelection())
					anchor = 0;
				Backspace(word);
			} else if (key == "Delete") {
				if (line && !HasSelection())
					anchor = int(text.size());
				Delete(word);
			} else if (command && EqualsIgnoringCase(key, "a")) {
				SelectAll();
			} else if (command && (EqualsIgnoringCase(key, "c") || EqualsIgnoringCase(key, "x"))) {
				if (HasSelection()) {
					SDL_SetClipboardText(SelectedText().c_str());
					if (EqualsIgnoringCase(key, "x")) {
						Remember(Edit::Other);
						EraseSelection();
						Changed();
					}
				}
			} else if (command && EqualsIgnoringCase(key, "v")) {
				if (char* pasted = SDL_GetClipboardText()) {
					const std::string s = pasted;
					SDL_free(pasted);
					lastEdit = Edit::Other; // a paste undoes on its own
					Insert(s);
					lastEdit = Edit::Other;
				}
			} else if (command && EqualsIgnoringCase(key, "z")) {
				if (m.shift)
					Redo();
				else
					Undo();
			} else if (command && EqualsIgnoringCase(key, "y")) {
				Redo();
			} else {
				return false;
			}
			return true;
		}

		int TextEntry::IndexAt(float x, int from,
		                       const std::function<float(const std::string&)>& width) const {
			int best = from;
			float bestDistance = std::fabs(x);
			for (int i = Next(from); i <= int(text.size());) {
				const float w = width(text.substr(std::size_t(from), std::size_t(i - from)));
				const float distance = std::fabs(w - x);
				if (distance < bestDistance) {
					best = i;
					bestDistance = distance;
				} else if (w > x) {
					break; // only further from here
				}
				if (i == int(text.size()))
					break;
				i = Next(i);
			}
			return best;
		}
	} // namespace gui
} // namespace spades
