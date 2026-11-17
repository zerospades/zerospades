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

#include "StepSlider.h"

#include <algorithm>
#include <cmath>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>

#include "EditorTheme.h"

namespace spades {
	namespace gui {
		namespace {
			constexpr float kValueH = 18.0F; // the chosen step's label, over the handle
			constexpr float kTrackH = 18.0F; // the band the track and handle sit in
			constexpr float kLabelH = 16.0F; // the major steps' labels, under it
			constexpr float kInset = 12.0F;  // the end steps, in from the sides
			constexpr float kHandleR = 7.0F;
			constexpr float kTextScale = 0.8F;
		} // namespace

		StepSlider::StepSlider(client::IRenderer& renderer, client::IFont& font)
		    : renderer(renderer), font(font) {}

		float StepSlider::Height() { return kValueH + kTrackH + kLabelH; }

		void StepSlider::SetSteps(std::vector<Step> newSteps, int initial) {
			steps = std::move(newSteps);
			held = false;
			index = steps.empty() ? 0 : NearestUsable(std::max(0, std::min(Count() - 1, initial)));
		}

		void StepSlider::SetUsable(int i, bool usable) {
			if (i < 0 || i >= Count())
				return;
			steps[std::size_t(i)].usable = usable;
			if (!usable && i == index)
				index = NearestUsable(index);
		}

		void StepSlider::Layout(float left, float top, float w) {
			x = left;
			y = top;
			width = w;
		}

		bool StepSlider::Contains(const Vector2& at) const {
			return OverlayInRect(at, x, y, width, Height());
		}

		float StepSlider::StepX(int i) const {
			if (Count() < 2)
				return x + width * 0.5F;
			return x + kInset + (width - kInset * 2.0F) * float(i) / float(Count() - 1);
		}

		int StepSlider::NearestUsable(int from) const {
			for (int d = 0; d < Count(); d++) {
				for (int i : {from - d, from + d}) {
					if (i >= 0 && i < Count() && steps[std::size_t(i)].usable)
						return i;
				}
			}
			return from; // none usable: it stays put
		}

		int StepSlider::NearestAt(float at) const {
			int best = -1;
			float bestDistance = 0.0F;
			for (int i = 0; i < Count(); i++) {
				if (!steps[std::size_t(i)].usable)
					continue;
				const float d = std::fabs(StepX(i) - at);
				if (best < 0 || d < bestDistance) {
					best = i;
					bestDistance = d;
				}
			}
			return best;
		}

		bool StepSlider::Choose(int i) {
			if (i < 0 || i == index)
				return false;
			index = i;
			return true;
		}

		bool StepSlider::Press(const Vector2& at) {
			if (!Contains(at) || steps.empty())
				return false;
			held = true;
			return Choose(NearestAt(at.x));
		}

		bool StepSlider::Drag(const Vector2& at) { return held && Choose(NearestAt(at.x)); }

		bool StepSlider::Key(const std::string& key) {
			auto step = [&](int from, int direction) {
				for (int i = from + direction; i >= 0 && i < Count(); i += direction)
					if (steps[std::size_t(i)].usable)
						return Choose(i);
				return false;
			};
			if (key == "Left")
				return step(index, -1);
			if (key == "Right")
				return step(index, 1);
			if (key == "Home")
				return step(-1, 1);
			if (key == "End")
				return step(Count(), -1);
			return false;
		}

		void StepSlider::Draw(const Vector2& cursor) {
			if (steps.empty())
				return;
			const float trackY = y + kValueH + kTrackH * 0.5F;
			const float labelH = font.Measure("Ag").y * kTextScale;
			auto centred = [&](const std::string& s, float cx, float ty, const Vector4& c) {
				const float w = font.Measure(s).x * kTextScale;
				font.DrawShadow(s, MakeVector2(cx - w * 0.5F, ty), kTextScale, c,
				                EditorTheme::TextShadow());
			};

			// The track, filled up to the handle from its first step.
			OverlayColorNP(renderer, EditorTheme::Separator());
			OverlayFillRect(renderer, StepX(0), trackY - 1.5F, StepX(Count() - 1) - StepX(0), 3.0F);
			OverlayColorNP(renderer, EditorTheme::Active());
			OverlayFillRect(renderer, StepX(0), trackY - 1.5F, StepX(index) - StepX(0), 3.0F);

			// A tick for every step, longer and labelled for the major ones.
			for (int i = 0; i < Count(); i++) {
				const Step& s = steps[std::size_t(i)];
				const float tx = StepX(i);
				const float tick = s.major ? 6.0F : 3.5F;
				OverlayColorNP(renderer, s.usable ? EditorTheme::TextDim() : EditorTheme::TextFaint());
				OverlayFillRect(renderer, tx - 0.5F, trackY + 3.0F, 1.0F, tick);
				if (s.major)
					centred(s.label, tx, y + kValueH + kTrackH, s.usable ? EditorTheme::TextDim()
					                                                      : EditorTheme::TextFaint());
			}

			// The handle, lit while it is held or the pointer is on it.
			const float hx = StepX(index);
			const bool hot = held || OverlayInRect(cursor, hx - kHandleR - 2.0F,
			                                       trackY - kHandleR - 2.0F, kHandleR * 2.0F + 4.0F,
			                                       kHandleR * 2.0F + 4.0F);
			OverlayFillCircle(renderer, MakeVector2(hx, trackY), kHandleR + 1.5F,
			                  EditorTheme::PanelBorder());
			OverlayFillCircle(renderer, MakeVector2(hx, trackY), kHandleR,
			                  hot ? EditorTheme::Text() : EditorTheme::Active());

			// The chosen step, over the handle, kept inside the box.
			const std::string& value = steps[std::size_t(index)].label;
			const float vw = font.Measure(value).x * kTextScale;
			const float vx = std::max(x + vw * 0.5F, std::min(x + width - vw * 0.5F, hx));
			centred(value, vx, y + (kValueH - labelH) * 0.5F, EditorTheme::Text());
		}
	} // namespace gui
} // namespace spades
