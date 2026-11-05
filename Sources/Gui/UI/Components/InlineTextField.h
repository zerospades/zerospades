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
#include <string>

#include <Core/Math.h>
#include <Gui/UI/Framework/DoubleClick.h>
#include <Gui/UI/Widgets/TextEntry.h>

namespace spades {
	namespace client {
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * A one-line text box drawn in place of something's label (a name in a
		 * list, a value in a bar) while it is being typed: the TextEntry being
		 * edited, shown in a box with the caret blinking, the selection, the
		 * text an input method has yet to settle, and the text scrolling so the
		 * caret stays in sight.
		 *
		 * Its owner places it each frame (SetRect), routes it the pointer and
		 * the keys while it is open, and decides what Enter and Escape do.
		 */
		class InlineTextField {
		public:
			InlineTextField(client::IRenderer& renderer, client::IFont& font, float textScale,
			                std::size_t maxBytes);

			/** Opens on `text`, all of it selected or the caret at its end. */
			void Begin(const std::string& text, bool selectAll, float time);
			void End();
			bool IsOpen() const { return entry.IsEditing(); }
			const std::string& Text() const { return entry.Text(); }

			/** Where it is drawn, and so where it takes the pointer. */
			void SetRect(const AABB2& rect) { box = rect; }
			const AABB2& Rect() const { return box; }
			bool Contains(const Vector2& at) const;

			/** A left press inside: the caret goes there (Shift: the selection
			 *  reaches there), a double one selects the word. */
			void Press(const Vector2& at, bool extend, float time);
			/** The pointer moving with the button held since a press inside. */
			void Drag(const Vector2& at);
			/** An editing key (TextEntry::Key); false when it is not one, or an
			 *  input method has the keys mid-composition. */
			bool Key(const std::string& key, const TextEntry::Modifiers& modifiers);
			/** What was typed, and what an input method has yet to settle. */
			void Insert(const std::string& text);
			void Compose(const std::string& text) { composing = text; }

			/** `refused` outlines it as a text that would not do. */
			void Draw(float time, bool refused);

		private:
			client::IRenderer& renderer;
			client::IFont& font;
			float textScale;
			TextEntry entry;
			std::string composing;
			AABB2 box;
			int first = 0; // the first byte in sight
			float caretSince = 0.0F;
			unsigned long caretRevision = ~0UL;
			ui::DoubleClick clicks;

			float Width(const std::string& text) const;
			float TextLeft() const;
			float TextWidth() const;
			/** Keeps the caret, and what is being composed, in sight. */
			void Scroll();
			int IndexAt(float x) const;
		};
	} // namespace gui
} // namespace spades
