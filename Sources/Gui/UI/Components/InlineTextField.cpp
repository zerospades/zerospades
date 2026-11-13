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

#include "InlineTextField.h"

#include <algorithm>
#include <cmath>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UIWidgetPainter.h>

#include "EditorTheme.h"

namespace spades {
	namespace gui {
		namespace {
			constexpr float kPad = 3.0F;        // the text, in from the box
			constexpr float kCaretBlink = 1.06F; // seconds the caret blinks in
		} // namespace

		InlineTextField::InlineTextField(client::IRenderer& renderer, client::IFont& font,
		                                 float textScale, std::size_t maxBytes)
		    : renderer(renderer), font(font), textScale(textScale), entry(maxBytes) {}

		void InlineTextField::Begin(const std::string& text, bool selectAll, float time) {
			entry.Begin(text, selectAll);
			composing.clear();
			first = 0;
			caretSince = time;
			caretRevision = entry.Revision();
			clicks.Reset();
		}

		void InlineTextField::End() {
			entry.End();
			composing.clear();
		}

		bool InlineTextField::Contains(const Vector2& at) const {
			return IsOpen() && OverlayInRect(at, box.GetMinX(), box.GetMinY(), box.GetWidth(),
			                                 box.GetHeight());
		}

		float InlineTextField::Width(const std::string& text) const {
			return text.empty() ? 0.0F : font.Measure(text).x * textScale;
		}

		float InlineTextField::TextLeft() const { return box.GetMinX() + kPad; }

		float InlineTextField::TextWidth() const { return box.GetWidth() - kPad * 2.0F; }

		int InlineTextField::IndexAt(float x) const {
			return entry.IndexAt(x - TextLeft(), first,
			                     [this](const std::string& t) { return Width(t); });
		}

		void InlineTextField::Press(const Vector2& at, bool extend, float time) {
			composing.clear();
			const int i = IndexAt(at.x);
			if (!extend && clicks.Click(at, time))
				entry.SelectWordAt(i);
			else
				entry.SetCaret(i, extend);
		}

		void InlineTextField::Drag(const Vector2& at) { entry.SetCaret(IndexAt(at.x), true); }

		bool InlineTextField::Key(const std::string& key, const TextEntry::Modifiers& modifiers) {
			if (!composing.empty())
				return false; // the input method has the keys
			return entry.Key(key, modifiers);
		}

		void InlineTextField::Insert(const std::string& text) {
			composing.clear();
			entry.Insert(text);
		}

		void InlineTextField::Scroll() {
			const std::string& text = entry.Text();
			const int caret = entry.Caret();
			const float width = TextWidth();
			first = std::min(first, caret);
			// Right until the caret, with what is being composed, fits...
			while (first < caret &&
			       Width(text.substr(std::size_t(first), std::size_t(caret - first)) + composing) >
			         width)
				first = entry.Next(first);
			// ...and back left while what lies before fits too, so the box never
			// shows less of the text than it could.
			while (first > 0 &&
			       Width(text.substr(std::size_t(entry.Previous(first))) + composing) <= width)
				first = entry.Previous(first);
		}

		void InlineTextField::Draw(float time, bool refused) {
			if (!IsOpen())
				return;
			Scroll();
			const float x = box.GetMinX(), y = box.GetMinY();
			const float w = box.GetWidth(), h = box.GetHeight();
			const float left = TextLeft(), width = TextWidth();

			// Opaque under the field's own chrome, so it reads over whatever the
			// view shows behind it.
			OverlayColorNP(renderer, EditorTheme::FieldFill());
			OverlayFillRect(renderer, x, y, w, h);
			widgets::PaintField(renderer, MakeVector2(x, y), MakeVector2(w, h), true, false);
			OverlayStrokeRect(renderer, x, y, w, h, 1.0F,
			                  refused ? EditorTheme::Refused() : EditorTheme::DropTarget());

			const std::string& text = entry.Text();
			const int caret = entry.Caret();
			auto widthTo = [&](int end) {
				return Width(text.substr(std::size_t(first), std::size_t(std::max(0, end - first))));
			};

			if (composing.empty() && entry.HasSelection()) {
				const float x0 = left + widthTo(std::max(entry.SelectionStart(), first));
				const float x1 = std::min(left + widthTo(entry.SelectionEnd()), left + width);
				if (x1 > x0) {
					OverlayColorNP(renderer, EditorTheme::TextSelection());
					OverlayFillRect(renderer, x0, y + 2.0F, x1 - x0, h - 4.0F);
				}
			}

			// What is in sight: from the first byte shown, what is being composed
			// put in at the caret, cut where the box ends.
			std::string shown = text.substr(std::size_t(first));
			shown.insert(std::size_t(caret - first), composing);
			while (!shown.empty() && Width(shown) > width) {
				std::size_t end = shown.size() - 1;
				while (end > 0 && (static_cast<unsigned char>(shown[end]) & 0xC0) == 0x80)
					end--;
				shown.erase(end);
			}
			const float textH = font.Measure("Ag").y * textScale;
			font.DrawShadow(shown, MakeVector2(left, y + (h - textH) * 0.5F), textScale,
			                EditorTheme::Text(), EditorTheme::TextShadow());

			const float caretX = left + widthTo(caret);
			if (!composing.empty()) {
				// Underlined until the input method settles it.
				OverlayColorNP(renderer, EditorTheme::Text());
				OverlayFillRect(renderer, caretX, y + h - 3.0F,
				                std::min(Width(composing), left + width - caretX), 1.0F);
			}
			// Steady while it moves, then blinking.
			if (entry.Revision() != caretRevision) {
				caretRevision = entry.Revision();
				caretSince = time;
			}
			if (std::fmod(time - caretSince, kCaretBlink) < kCaretBlink * 0.5F) {
				OverlayColorNP(renderer, EditorTheme::Caret());
				OverlayFillRect(renderer, caretX + Width(composing), y + 3.0F, 1.5F, h - 6.0F);
			}
		}
	} // namespace gui
} // namespace spades
