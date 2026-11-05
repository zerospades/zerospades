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

#include "LowPolyPanel.h"

#include <algorithm>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UI/Components/EditorTheme.h>
#include <Gui/UIWidgetPainter.h>

namespace spades {
	namespace gui {
		namespace {
			// The factors offered: one voxel for 2 each way, and up.
			const int kFactors[] = {2, 3, 4, 5, 6, 8, 12, 16};
			constexpr int kFactorCount = int(sizeof(kFactors) / sizeof(kFactors[0]));

			constexpr float kWidth = 320.0F;
			constexpr float kPad = 12.0F;
			constexpr float kTitleH = 22.0F;
			constexpr float kLabelH = 18.0F;
			constexpr float kChipH = 24.0F;
			constexpr float kChipGap = 4.0F;
			constexpr float kLineH = 17.0F;
			constexpr float kButtonW = 84.0F;
			constexpr float kButtonH = 26.0F;
			constexpr float kHeight =
			  kPad + kTitleH + kLabelH + kChipH + 8.0F + kLineH * 2.0F + 10.0F + kButtonH + 6.0F +
			  kLineH + kPad - 6.0F;

			// A count with thousands set apart: 41,232.
			std::string Count(long n) {
				std::string digits = std::to_string(n);
				for (int i = int(digits.size()) - 3; i > 0; i -= 3)
					digits.insert(std::size_t(i), ",");
				return digits;
			}

			std::string Size(const IntVector3& s) {
				return std::to_string(s.x) + " x " + std::to_string(s.y) + " x " + std::to_string(s.z);
			}
		} // namespace

		LowPolyPanel::LowPolyPanel(client::IRenderer& renderer, client::IFont& font,
		                           client::IAudioDevice* audioDevice)
		    : renderer(renderer), font(font), sounds(audioDevice) {}

		void LowPolyPanel::Open(const std::string& nodeName, int initial) {
			open = true;
			name = nodeName;
			factor = initial;
			pressed = Button::None;
			voxels = sourceVoxels = 0;
		}

		void LowPolyPanel::SetStats(const IntVector3& fromSize, long fromVoxels, const IntVector3& toSize,
		                            long toVoxels) {
			sourceSize = fromSize;
			sourceVoxels = fromVoxels;
			size = toSize;
			voxels = toVoxels;
		}

		void LowPolyPanel::Layout(float left, float top) {
			x = left;
			y = top;
		}

		bool LowPolyPanel::Contains(const Vector2& at) const {
			return open && OverlayInRect(at, x, y, kWidth, kHeight);
		}

		void LowPolyPanel::ChipRect(int index, float& cx, float& cy, float& cw, float& ch) const {
			cw = (kWidth - kPad * 2.0F - kChipGap * float(kFactorCount - 1)) / float(kFactorCount);
			ch = kChipH;
			cx = x + kPad + float(index) * (cw + kChipGap);
			cy = y + kPad + kTitleH + kLabelH;
		}

		void LowPolyPanel::ButtonRect(Button button, float& bx, float& by, float& bw,
		                              float& bh) const {
			bw = kButtonW;
			bh = kButtonH;
			by = y + kPad + kTitleH + kLabelH + kChipH + 8.0F + kLineH * 2.0F + 10.0F;
			// Create on the right, where a dialog's way forward is.
			bx = x + kWidth - kPad - kButtonW;
			if (button == Button::Cancel)
				bx -= kButtonW + 8.0F;
		}

		int LowPolyPanel::ChipAt(const Vector2& at) const {
			for (int i = 0; i < kFactorCount; i++) {
				float cx, cy, cw, ch;
				ChipRect(i, cx, cy, cw, ch);
				if (OverlayInRect(at, cx, cy, cw, ch))
					return i;
			}
			return -1;
		}

		LowPolyPanel::Button LowPolyPanel::ButtonAt(const Vector2& at) const {
			for (Button b : {Button::Cancel, Button::Create}) {
				float bx, by, bw, bh;
				ButtonRect(b, bx, by, bw, bh);
				if (OverlayInRect(at, bx, by, bw, bh))
					return b;
			}
			return Button::None;
		}

		LowPolyPanel::Action LowPolyPanel::Press(const Vector2& at) {
			pressed = Button::None;
			if (!Contains(at))
				return Action::None;
			const int chip = ChipAt(at);
			if (chip >= 0) {
				if (kFactors[chip] == factor)
					return Action::None;
				factor = kFactors[chip];
				sounds.Activate();
				return Action::Changed;
			}
			pressed = ButtonAt(at);
			return Action::None;
		}

		LowPolyPanel::Action LowPolyPanel::Release(const Vector2& at) {
			const Button was = pressed;
			pressed = Button::None;
			if (was == Button::None || ButtonAt(at) != was)
				return Action::None;
			if (was == Button::Create && !CanCreate())
				return Action::None;
			sounds.Activate();
			return was == Button::Create ? Action::Create : Action::Cancel;
		}

		LowPolyPanel::Action LowPolyPanel::Step(int direction) {
			int index = 0;
			while (index < kFactorCount - 1 && kFactors[index] < factor)
				index++;
			const int next = std::max(0, std::min(kFactorCount - 1, index + direction));
			if (kFactors[next] == factor)
				return Action::None;
			factor = kFactors[next];
			sounds.Hover();
			return Action::Changed;
		}

		LowPolyPanel::Action LowPolyPanel::Key(const std::string& key) {
			if (!open)
				return Action::None;
			if (key == "Escape")
				return Action::Cancel;
			if (key == "Enter" || key == "Keypad Enter")
				return CanCreate() ? Action::Create : Action::None;
			if (key == "Left" || key == "-")
				return Step(-1);
			if (key == "Right" || key == "=" || key == "+")
				return Step(1);
			// A digit picks that factor, when it is one offered.
			if (key.size() == 1 && key[0] >= '2' && key[0] <= '9') {
				const int wanted = key[0] - '0';
				for (int f : kFactors) {
					if (f == wanted && f != factor) {
						factor = f;
						sounds.Hover();
						return Action::Changed;
					}
				}
				return Action::None;
			}
			return Action::None;
		}

		void LowPolyPanel::Draw(const Vector2& cursor) {
			if (!open)
				return;
			const float textScale = 0.9F;
			auto text = [&](const std::string& s, float tx, float ty, float scale, const Vector4& c) {
				font.DrawShadow(s, MakeVector2(tx, ty), scale, c, EditorTheme::TextShadow());
			};

			EditorTheme::PaintPanel(renderer, x, y, kWidth, kHeight);

			float ty = y + kPad;
			text("Low-poly KV6", x + kPad, ty, 1.0F, EditorTheme::Text());
			const float titleW = font.Measure("Low-poly KV6  ").x;
			std::string shown = name;
			while (!shown.empty() && font.Measure(shown).x * textScale > kWidth - kPad * 2.0F - titleW)
				shown.pop_back();
			text(shown == name ? shown : shown + "...", x + kPad + titleW, ty + 1.0F, textScale,
			     EditorTheme::TextDim());
			ty += kTitleH;
			text("Resolution: one voxel for every N each way", x + kPad, ty, 0.8F,
			     EditorTheme::TextDim());

			// The factors, as the toolbar's buttons: the chosen one held down.
			const int overChip = ChipAt(cursor);
			if (overChip != hoverChip && overChip >= 0 && kFactors[overChip] != factor)
				sounds.Hover();
			hoverChip = overChip;
			for (int i = 0; i < kFactorCount; i++) {
				float cx, cy, cw, ch;
				ChipRect(i, cx, cy, cw, ch);
				widgets::PaintButton(renderer, font, MakeVector2(cx, cy), MakeVector2(cw, ch),
				                     "1/" + std::to_string(kFactors[i]), MakeVector2(0.5F, 0.5F),
				                     std::string(), MakeVector2(1.0F, 0.5F), true, i == overChip,
				                     false, kFactors[i] == factor, textScale);
			}

			ty = y + kPad + kTitleH + kLabelH + kChipH + 8.0F;
			if (voxels > 0) {
				text(Size(sourceSize) + "  ->  " + Size(size), x + kPad, ty, 0.85F, EditorTheme::Text());
				text(Count(sourceVoxels) + " voxels  ->  " + Count(voxels), x + kPad, ty + kLineH, 0.85F,
				     EditorTheme::Text());
			} else {
				text("Too coarse: nothing of it is left.", x + kPad, ty, 0.85F, EditorTheme::Refused());
				text("Choose a finer resolution.", x + kPad, ty + kLineH, 0.85F, EditorTheme::TextDim());
			}

			const Button over = ButtonAt(cursor);
			if (over != hoverButton && over != Button::None)
				sounds.Hover();
			hoverButton = over;
			for (Button b : {Button::Cancel, Button::Create}) {
				float bx, by, bw, bh;
				ButtonRect(b, bx, by, bw, bh);
				const bool enabled = b != Button::Create || CanCreate();
				widgets::PaintButton(renderer, font, MakeVector2(bx, by), MakeVector2(bw, bh),
				                     b == Button::Create ? "Create" : "Cancel", MakeVector2(0.5F, 0.5F),
				                     std::string(), MakeVector2(1.0F, 0.5F), enabled,
				                     enabled && b == over, pressed == b, false, textScale);
			}

			// The keys.
			const float hy = y + kHeight - kPad - kLineH + 6.0F;
			text("[Left/Right] resolution   [Enter] create   [Esc] cancel", x + kPad, hy, 0.75F,
			     EditorTheme::TextFaint());
		}
	} // namespace gui
} // namespace spades
