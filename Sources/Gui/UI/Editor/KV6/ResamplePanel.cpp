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

#include "ResamplePanel.h"

#include <algorithm>
#include <cctype>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UI/Components/EditorTheme.h>
#include <Gui/UIWidgetPainter.h>

namespace spades {
	namespace gui {
		namespace {
			constexpr float kWidth = 340.0F;
			constexpr float kPad = 12.0F;
			constexpr float kTitleH = 22.0F;
			constexpr float kLabelH = 16.0F;
			constexpr float kLineH = 17.0F;
			constexpr float kButtonW = 84.0F;
			constexpr float kButtonH = 26.0F;

			float SliderTop() { return kPad + kTitleH + kLabelH; }
			float StatsTop() { return SliderTop() + StepSlider::Height() + 6.0F; }
			float ButtonsTop() { return StatsTop() + kLineH * 2.0F + 10.0F; }
			float Height() { return ButtonsTop() + kButtonH + 6.0F + kLineH + kPad - 6.0F; }

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

		ResamplePanel::ResamplePanel(client::IRenderer& renderer, client::IFont& font,
		                             client::IAudioDevice* audioDevice)
		    : renderer(renderer), font(font), sounds(audioDevice), slider(renderer, font) {}

		void ResamplePanel::Open(const std::string& nodeName, std::vector<StepSlider::Step> steps,
		                         int index) {
			open = true;
			name = nodeName;
			slider.SetSteps(std::move(steps), index);
			pressed = Button::None;
			voxels = sourceVoxels = 0;
			refused.clear();
		}

		void ResamplePanel::SetResult(const IntVector3& fromSize, long fromVoxels,
		                              const IntVector3& toSize, long toVoxels,
		                              const std::string& why) {
			sourceSize = fromSize;
			sourceVoxels = fromVoxels;
			size = toSize;
			voxels = toVoxels;
			refused = why;
		}

		void ResamplePanel::Layout(float left, float top) {
			x = left;
			y = top;
			slider.Layout(x + kPad, y + SliderTop(), kWidth - kPad * 2.0F);
		}

		bool ResamplePanel::Contains(const Vector2& at) const {
			return open && OverlayInRect(at, x, y, kWidth, Height());
		}

		void ResamplePanel::ButtonRect(Button button, float& bx, float& by, float& bw,
		                               float& bh) const {
			bw = kButtonW;
			bh = kButtonH;
			by = y + ButtonsTop();
			// Create on the right, where a dialog's way forward is.
			bx = x + kWidth - kPad - kButtonW;
			if (button == Button::Cancel)
				bx -= kButtonW + 8.0F;
		}

		ResamplePanel::Button ResamplePanel::ButtonAt(const Vector2& at) const {
			for (Button b : {Button::Cancel, Button::Create}) {
				float bx, by, bw, bh;
				ButtonRect(b, bx, by, bw, bh);
				if (OverlayInRect(at, bx, by, bw, bh))
					return b;
			}
			return Button::None;
		}

		ResamplePanel::Action ResamplePanel::Moved(bool moved) {
			if (!moved)
				return Action::None;
			sounds.Hover();
			return Action::Changed;
		}

		ResamplePanel::Action ResamplePanel::Press(const Vector2& at) {
			pressed = Button::None;
			if (!Contains(at))
				return Action::None;
			if (slider.Contains(at))
				return Moved(slider.Press(at));
			pressed = ButtonAt(at);
			return Action::None;
		}

		ResamplePanel::Action ResamplePanel::Move(const Vector2& at) {
			return Moved(slider.Drag(at));
		}

		ResamplePanel::Action ResamplePanel::Release(const Vector2& at) {
			slider.Release();
			const Button was = pressed;
			pressed = Button::None;
			if (was == Button::None || ButtonAt(at) != was)
				return Action::None;
			if (was == Button::Create && !CanCreate())
				return Action::None;
			sounds.Activate();
			return was == Button::Create ? Action::Create : Action::Cancel;
		}

		ResamplePanel::Action ResamplePanel::Key(const std::string& key) {
			if (!open)
				return Action::None;
			if (key == "Escape")
				return Action::Cancel;
			if (key == "Enter" || key == "Keypad Enter")
				return CanCreate() ? Action::Create : Action::None;
			return Moved(slider.Key(key));
		}

		void ResamplePanel::Draw(const Vector2& cursor) {
			if (!open)
				return;
			const float textScale = 0.9F;
			auto text = [&](const std::string& s, float tx, float ty, float scale, const Vector4& c) {
				font.DrawShadow(s, MakeVector2(tx, ty), scale, c, EditorTheme::TextShadow());
			};

			EditorTheme::PaintPanel(renderer, x, y, kWidth, Height());

			float ty = y + kPad;
			const std::string title = "Generate KV6";
			text(title, x + kPad, ty, 1.0F, EditorTheme::Text());
			const float titleW = font.Measure(title + "  ").x;
			std::string shown = name;
			while (!shown.empty() && font.Measure(shown).x * textScale > kWidth - kPad * 2.0F - titleW)
				shown.pop_back();
			text(shown == name ? shown : shown + "...", x + kPad + titleW, ty + 1.0F, textScale,
			     EditorTheme::TextDim());
			ty += kTitleH;
			text("Voxels of the KV6 for each voxel of the scene", x + kPad, ty, 0.8F,
			     EditorTheme::TextDim());

			slider.Draw(cursor);

			ty = y + StatsTop();
			if (refused.empty() && voxels > 0) {
				text(Size(sourceSize) + "  ->  " + Size(size), x + kPad, ty, 0.85F, EditorTheme::Text());
				text(Count(sourceVoxels) + " voxels  ->  " + Count(voxels), x + kPad, ty + kLineH, 0.85F,
				     EditorTheme::Text());
			} else {
				std::string why = refused.empty() ? std::string("Nothing of it is left.") : refused;
				why[0] = char(std::toupper(static_cast<unsigned char>(why[0])));
				text(why, x + kPad, ty, 0.8F, EditorTheme::Refused());
				text("Choose another resolution.", x + kPad, ty + kLineH, 0.85F, EditorTheme::TextDim());
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
			const float hy = y + Height() - kPad - kLineH + 6.0F;
			text("[Left/Right] resolution   [Enter] create   [Esc] cancel", x + kPad, hy, 0.75F,
			     EditorTheme::TextFaint());
		}
	} // namespace gui
} // namespace spades
