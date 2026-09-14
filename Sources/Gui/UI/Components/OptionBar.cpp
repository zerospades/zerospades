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

#include "OptionBar.h"

#include <Gui/OverlayPaint.h>
#include <Gui/UIWidgetPainter.h>
#include <Client/IAudioChunk.h>
#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Client/Fonts.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		// Layout constants
		static const float kRibbonH = 24.0F;
		static const float kToolbarH = 32.0F;
		static const float kSubBarH = 28.0F;
		static const float kTbH = 24.0F;
		static const float kTbGap = 2.0F;
		static const float kTbSep = 14.0F;
		static const float kTbX0 = 12.0F;
		static const float kSubBtn = 88.0F;
		static const float kMirW = 24.0F;
		static const float kMirLabelW = 50.0F;
		static const float kColorW = 46.0F;
		static const float kLabelW = 190.0F;

		OptionBar::OptionBar(client::IAudioDevice* audioDevice) : audioDevice(audioDevice) {}

		void OptionBar::PlayHoverSound() const {
			if (!audioDevice)
				return;
			Handle<client::IAudioChunk> chunk(audioDevice->RegisterSound("Sounds/Feedback/Limbo/Hover.opus"));
			audioDevice->PlayLocal(chunk.GetPointerOrNull(), client::AudioParam());
		}

		void OptionBar::PlayClickSound() const {
			if (!audioDevice)
				return;
			Handle<client::IAudioChunk> chunk(audioDevice->RegisterSound("Sounds/Feedback/Limbo/Select.opus"));
			audioDevice->PlayLocal(chunk.GetPointerOrNull(), client::AudioParam());
		}

		void OptionBar::SetSubToolButtons(const std::vector<SubToolButton>& buttons) {
			subToolButtons = buttons;
		}

		void OptionBar::SetOptions(const std::vector<Option>& options) { this->options = options; }

		bool OptionBar::InRect(const Vector2& p, float x, float y, float w, float h) const {
			return OverlayInRect(p, x, y, w, h);
		}

		float OptionBar::OptionX(int i, float& outW) const {
			outW = 0.0F;
			if (i < 0 || i >= int(options.size()))
				return 0.0F;

			float x = kTbX0 + float(subToolButtons.size()) * (kSubBtn + kTbGap);
			std::string prevGroup;
			bool first = true;

			for (int k = 0; k <= i; k++) {
				const Option& op = options[k];
				bool newGroup = first || op.group != prevGroup;

				if (newGroup) {
					x += kTbSep; // separator before a new group
					if (!op.group.empty())
						x += kMirLabelW; // room for the group label
				} else {
					x += kTbGap; // gap between items in the same group
				}

				float w = (op.type == OptionType::Color) ? kColorW
				                                        : (op.type == OptionType::Label ? kLabelW : kMirW);
				if (k == i) {
					outW = w;
					return x;
				}

				x += w;
				prevGroup = op.group;
				first = false;
			}
			return x;
		}

		float OptionBar::HitTest(const Vector2& p) {
			float bandY = kRibbonH + kToolbarH;
			float by = bandY + (kSubBarH - kTbH) * 0.5F;

			for (int i = 0; i < int(options.size()); i++) {
				float w;
				float x = OptionX(i, w);
				if (InRect(p, x, by, w, kTbH))
					return float(i);
			}
			return -1.0F;
		}

		bool OptionBar::IsSubToolButtonHit(const Vector2& p, int& outIndex) const {
			float bandY = kRibbonH + kToolbarH;
			float by = bandY + (kSubBarH - kTbH) * 0.5F;

			for (int i = 0; i < int(subToolButtons.size()); i++) {
				float x = kTbX0 + float(i) * (kSubBtn + kTbGap);
				if (InRect(p, x, by, kSubBtn, kTbH)) {
					outIndex = i;
					return true;
				}
			}
			return false;
		}

		bool OptionBar::IsOptionHovered(const Vector2& p, int index) const {
			if (index < 0 || index >= int(options.size()))
				return false;
			float bandY = kRibbonH + kToolbarH;
			float by = bandY + (kSubBarH - kTbH) * 0.5F;
			float w;
			float x = OptionX(index, w);
			return InRect(p, x, by, w, kTbH);
		}

		void OptionBar::Draw(client::IRenderer& renderer, client::FontManager& fontManager,
		                     const Vector2& cursorPos, bool menuActive, float screenWidth) {
			client::IFont& font = fontManager.GetSmallGuiFont();
			float s = 1.0F;
			float bandY = kRibbonH + kToolbarH;
			float by = bandY + (kSubBarH - kTbH) * 0.5F;

			// Ensure hover tracking arrays are sized correctly
			if (previousSubToolHoverState.size() != subToolButtons.size())
				previousSubToolHoverState.resize(subToolButtons.size(), false);
			if (previousOptionHoverState.size() != options.size())
				previousOptionHoverState.resize(options.size(), false);

			// Full-width sub-toolbar band background (always present)
			OverlayColorNP(renderer, MakeVector4(0.08F, 0.08F, 0.10F, 1.0F));
			OverlayFillRect(renderer, 0.0F, bandY, screenWidth, kSubBarH);

			// Draw sub-tool buttons
			for (int i = 0; i < int(subToolButtons.size()); i++) {
				float x = kTbX0 + float(i) * (kSubBtn + kTbGap);
				bool on = subToolButtons[i].active;
				bool hover = !menuActive && InRect(cursorPos, x, by, kSubBtn, kTbH);
				// Play sound on hover transition (false → true)
				if (hover && !previousSubToolHoverState[i])
					PlayHoverSound();
				previousSubToolHoverState[i] = hover;
				widgets::PaintButton(renderer, font, MakeVector2(x, by), MakeVector2(kSubBtn, kTbH),
				                     subToolButtons[i].label.c_str(), MakeVector2(0.5F, 0.5F), "",
				                     MakeVector2(1.0F, 0.5F), true, hover, false, on, s);
			}

			// Draw options (toggles, colours, labels)
			std::string prevGroup;
			bool first = true;
			for (int i = 0; i < int(options.size()); i++) {
				const Option& op = options[i];
				float w;
				float x = OptionX(i, w);
				bool newGroup = first || op.group != prevGroup;

				if (newGroup) {
					// Separator before the group; named groups also get a label
					float labelW = op.group.empty() ? 0.0F : kMirLabelW;
					OverlayColorNP(renderer, MakeVector4(0.5F, 0.5F, 0.5F, 0.4F));
					OverlayFillRect(renderer, x - labelW - kTbSep * 0.5F, by + 2.0F, 1.0F,
					               kTbH - 4.0F);
					if (!op.group.empty()) {
						font.Draw(op.group, MakeVector2(x - kMirLabelW + 2.0F, by + (kTbH - 9.0F * s) * 0.5F),
						         s, MakeVector4(0.75F, 0.75F, 0.75F, 1.0F));
					}
				}

				if (op.type == OptionType::Label) {
					Vector2 ts = font.Measure(op.label);
					font.Draw(op.label, MakeVector2(x, by + (kTbH - ts.y * s) * 0.5F), s,
					         MakeVector4(0.85F, 0.85F, 0.9F, 1.0F));
				} else if (op.type == OptionType::Color) {
					Vector4 color = MakeVector4(float(op.color & 0xFF) / 255.0F,
					                            float((op.color >> 8) & 0xFF) / 255.0F,
					                            float((op.color >> 16) & 0xFF) / 255.0F, 1.0F);
					OverlayColorNP(renderer, color);
					OverlayFillRect(renderer, x, by, w, kTbH);
					OverlayStrokeRect(renderer, x, by, w, kTbH, 1.0F,
					                 MakeVector4(0.8F, 0.8F, 0.8F, 0.7F));
				} else { // Bool toggle
					bool hover = !menuActive && InRect(cursorPos, x, by, w, kTbH);
					// Play sound on hover transition (false → true)
					if (hover && !previousOptionHoverState[i])
						PlayHoverSound();
					previousOptionHoverState[i] = hover;
					widgets::PaintButton(renderer, font, MakeVector2(x, by), MakeVector2(w, kTbH),
					                     op.label.c_str(), MakeVector2(0.5F, 0.5F), "",
					                     MakeVector2(1.0F, 0.5F), true, hover, false, op.bvalue, s);
				}

				prevGroup = op.group;
				first = false;
			}
		}
	} // namespace gui
} // namespace spades
