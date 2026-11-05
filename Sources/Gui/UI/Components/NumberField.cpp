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

#include "NumberField.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UI/Widgets/NumberEntry.h>
#include <Gui/UIWidgetPainter.h>

#include "EditorTheme.h"

namespace spades {
	namespace gui {
		namespace {
			// Pixels a press must travel before it scrubs rather than clicks.
			constexpr float kScrubStart = 3.0F;
			// Shift scrubs this much finer.
			constexpr float kFine = 0.1F;
			constexpr std::size_t kMaxTyped = 64;
			constexpr float kPadX = 6.0F;
		} // namespace

		NumberField::NumberField(client::IRenderer& renderer, client::IFont& font, float textScale)
		    : renderer(renderer),
		      font(font),
		      textScale(textScale),
		      text(renderer, font, textScale, kMaxTyped) {}

		void NumberField::SetRect(const AABB2& rect) {
			box = rect;
			text.SetRect(rect);
		}

		bool NumberField::Contains(const Vector2& at) const {
			return OverlayInRect(at, box.GetMinX(), box.GetMinY(), box.GetWidth(), box.GetHeight());
		}

		void NumberField::SetLabel(const std::string& l, const Vector4& color) {
			label = l;
			labelColor = color;
		}

		void NumberField::SetValue(float v) {
			if (!held && !IsTyping())
				value = v;
		}

		float NumberField::Clamped(float v) const {
			return std::max(format.minimum, std::min(format.maximum, v));
		}

		std::string NumberField::Formatted(float v) const {
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%.*f", std::max(0, format.decimals), double(v));
			std::string s = buf;
			// "-0.00" is 0.
			if (s[0] == '-' && s.find_first_not_of("-0.") == std::string::npos)
				s.erase(0, 1);
			return s;
		}

		bool NumberField::Typed(float& out) const {
			if (!NumberEntry::Evaluate(text.Text(), out))
				return false;
			out = Clamped(out);
			return true;
		}

		void NumberField::Press(const Vector2&) {
			if (!enabled)
				return;
			held = true;
			scrubbing = false;
			travel = 0.0F;
			startValue = value;
		}

		NumberField::Result NumberField::Drag(float dx, bool fine, bool inSteps) {
			if (!held)
				return Result::None;
			travel += dx;
			if (!scrubbing) {
				if (std::fabs(travel) < kScrubStart)
					return Result::None;
				scrubbing = true;
			}
			float v = startValue + travel * format.perPixel * (fine ? kFine : 1.0F);
			if (inSteps && format.step > 0.0F)
				v = std::round(v / format.step) * format.step;
			v = Clamped(v);
			if (v == value)
				return Result::None;
			value = v;
			return Result::Preview;
		}

		NumberField::Result NumberField::Release(float time) {
			if (!held)
				return Result::None;
			held = false;
			if (scrubbing) {
				scrubbing = false;
				return value == startValue ? Result::Cancel : Result::Commit;
			}
			BeginTyping(time); // a click: type it
			return Result::None;
		}

		NumberField::Result NumberField::CancelScrub() {
			if (!held)
				return Result::None;
			held = false;
			scrubbing = false;
			value = startValue;
			return Result::Cancel;
		}

		void NumberField::BeginTyping(float time) {
			if (!enabled)
				return;
			startValue = value;
			typedValid = true;
			text.Begin(Formatted(value), true, time);
		}

		NumberField::Result NumberField::Settle() {
			if (!IsTyping())
				return Result::None;
			float typed;
			const bool ok = Typed(typed);
			text.End();
			if (!ok || typed == startValue) {
				value = startValue;
				return Result::Cancel;
			}
			value = typed;
			return Result::Commit;
		}

		NumberField::Result NumberField::Key(const std::string& key,
		                                     const TextEntry::Modifiers& modifiers) {
			if (!IsTyping())
				return Result::None;
			if (key == "Escape") {
				text.End();
				value = startValue;
				return Result::Cancel;
			}
			if (key == "Enter" || key == "Keypad Enter" || key == "Tab") {
				float typed;
				if (!Typed(typed)) {
					typedValid = false; // stays open, outlined, to be put right
					return Result::None;
				}
				text.End();
				value = typed;
				if (key == "Tab")
					return modifiers.shift ? Result::Previous : Result::Next;
				return Result::Commit;
			}
			text.Key(key, modifiers);
			float typed;
			typedValid = text.Text().empty() || Typed(typed);
			return Result::None;
		}

		void NumberField::Draw(float time, bool hover) {
			if (IsTyping()) {
				text.Draw(time, !typedValid);
				return;
			}
			const float x = box.GetMinX(), y = box.GetMinY();
			const float w = box.GetWidth(), h = box.GetHeight();
			const bool lit = enabled && (hover || held);
			OverlayColorNP(renderer, EditorTheme::FieldFill());
			OverlayFillRect(renderer, x, y, w, h);
			widgets::PaintField(renderer, MakeVector2(x, y), MakeVector2(w, h), held, lit);
			if (scrubbing) // a bar along its foot says it is being dragged
				OverlayStrokeRect(renderer, x, y, w, h, 1.0F, EditorTheme::DropTarget());

			const float alpha = enabled ? 1.0F : 0.4F;
			const float textH = font.Measure("0").y * textScale;
			const float textY = y + (h - textH) * 0.5F;
			Vector4 axis = labelColor;
			axis.w *= alpha;
			font.DrawShadow(label, MakeVector2(x + kPadX, textY), textScale, axis,
			                EditorTheme::TextShadow());
			const std::string shown = Formatted(value) + format.suffix;
			const float shownW = font.Measure(shown).x * textScale;
			Vector4 color = EditorTheme::Text();
			color.w *= alpha;
			font.DrawShadow(shown, MakeVector2(x + w - kPadX - shownW, textY), textScale, color,
			                EditorTheme::TextShadow());
		}
	} // namespace gui
} // namespace spades
