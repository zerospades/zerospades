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

#include <algorithm>
#include <utility>

#include <Gui/OverlayPaint.h>
#include <Gui/UIWidgetPainter.h>
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
		static const float kBandY = kRibbonH + kToolbarH;
		static const float kBtnY = kBandY + (kSubBarH - kTbH) * 0.5F;
		static const float kSubBtn = 88.0F;
		// A readout is at least this wide, so the options after it hold still
		// while its text changes.
		static const float kLabelW = 190.0F;
		static const float kGroupLabelGap = 6.0F;
		// A button's text sits inside PaintButton's 8-unit inset on each side.
		static const float kButtonTextInset = 16.0F;
		static const float kToggleMinW = 24.0F;
		// An action carries a phrase, not an axis letter, so it gets more room
		// than a toggle; with the separators, that keeps it from reading as one
		// more sub-tool button that just failed to latch.
		static const float kActionPad = 24.0F;
		static const float kActionMinW = 56.0F;

		OptionBar::OptionBar(client::IAudioDevice* audioDevice) : sounds(audioDevice) {}

		void OptionBar::SetContent(const void* newOwner, std::vector<SubToolButton> buttons,
		                           std::vector<Option> newOptions) {
			owner = newOwner;
			subToolButtons = std::move(buttons);
			options = std::move(newOptions);
		}

		bool OptionBar::Click(const Vector2& p) {
			if (CurrentOwner && CurrentOwner() != drawnOwner)
				return false; // what is on screen belongs to an owner no longer current
			for (size_t i = 0; i < subToolSpans.size() && i < subToolButtons.size(); i++) {
				const Span& span = subToolSpans[i];
				if (!OverlayInRect(p, span.x, kBtnY, span.width, kTbH))
					continue;
				if (OnSubToolClicked)
					OnSubToolClicked(int(i));
				sounds.Activate();
				return true;
			}
			for (size_t i = 0; i < optionSpans.size() && i < options.size(); i++) {
				const Span& span = optionSpans[i];
				if (!OverlayInRect(p, span.x, kBtnY, span.width, kTbH))
					continue;
				const Option& op = options[i];
				const std::function<void(const std::string&)>* handler = nullptr;
				switch (op.type) {
					case OptionType::Bool: handler = &OnBoolToggled; break;
					case OptionType::Action: handler = &OnActionClicked; break;
					case OptionType::Label: break;
				}
				if (!handler || !op.enabled)
					return false; // a readout, or greyed out
				const std::string id = op.id; // the handler may change the options
				if (*handler)
					(*handler)(id);
				sounds.Activate();
				return true;
			}
			return false;
		}

		void OptionBar::Draw(client::IRenderer& renderer, client::FontManager& fontManager,
		                     const Vector2& cursorPos, bool menuActive, float screenWidth) {
			client::IFont& font = fontManager.GetSmallGuiFont();
			const Vector4 textColor = MakeVector4(0.85F, 0.85F, 0.9F, 1.0F);
			const Vector4 groupColor = MakeVector4(0.75F, 0.75F, 0.75F, 1.0F);

			previousSubToolHoverState.resize(subToolButtons.size(), false);
			previousOptionHoverState.resize(options.size(), false);
			drawnOwner = owner;
			subToolSpans.clear();
			optionSpans.clear();

			// Full-width sub-toolbar band background (always present)
			OverlayColorNP(renderer, MakeVector4(0.08F, 0.08F, 0.10F, 1.0F));
			OverlayFillRect(renderer, 0.0F, kBandY, screenWidth, kSubBarH);

			// Items are placed left to right, `x` being where the last one ended:
			// a gap parts items of one group, a separator line one group from the
			// next. Nothing precedes the first item.
			float x = kTbX0;
			bool placedAny = false;
			auto advance = [&](bool newGroup) {
				if (!placedAny) {
					placedAny = true;
					return;
				}
				if (!newGroup) {
					x += kTbGap;
					return;
				}
				OverlayColorNP(renderer, MakeVector4(0.5F, 0.5F, 0.5F, 0.4F));
				OverlayFillRect(renderer, x + kTbSep * 0.5F, kBtnY + 2.0F, 1.0F, kTbH - 4.0F);
				x += kTbSep;
			};
			// Hover of an item this frame, sounding once as it starts.
			auto hovered = [&](const Span& span, bool enabled, std::vector<bool>& previous,
			                   size_t i) {
				bool hover = !menuActive && enabled &&
				             OverlayInRect(cursorPos, span.x, kBtnY, span.width, kTbH);
				previous[i] = sounds.HoverEdge(hover, previous[i]);
				return hover;
			};

			for (size_t i = 0; i < subToolButtons.size(); i++) {
				const SubToolButton& button = subToolButtons[i];
				advance(false);
				const Span span{x, kSubBtn};
				subToolSpans.push_back(span);
				bool hover = hovered(span, true, previousSubToolHoverState, i);
				Vector2 labelAlign =
				  button.hotKey.empty() ? MakeVector2(0.5F, 0.5F) : MakeVector2(0.0F, 0.5F);
				widgets::PaintButton(renderer, font, MakeVector2(span.x, kBtnY),
				                     MakeVector2(span.width, kTbH), button.label, labelAlign,
				                     button.hotKey, MakeVector2(1.0F, 0.5F), true, hover, false,
				                     button.active, 1.0F);
				x += span.width;
			}

			for (size_t i = 0; i < options.size(); i++) {
				const Option& op = options[i];
				const bool startsGroup = i == 0 || op.group != options[i - 1].group;
				advance(startsGroup);
				if (startsGroup && !op.group.empty()) {
					Vector2 ts = font.Measure(op.group);
					font.Draw(op.group, MakeVector2(x, kBtnY + (kTbH - ts.y) * 0.5F), 1.0F,
					          groupColor);
					x += ts.x + kGroupLabelGap;
				}

				const Vector2 textSize = font.Measure(op.label);
				float width = 0.0F;
				switch (op.type) {
					case OptionType::Label:
						width = std::max(kLabelW, textSize.x);
						break;
					case OptionType::Bool:
						width = std::max(kToggleMinW, textSize.x + kButtonTextInset);
						break;
					case OptionType::Action:
						width = std::max(kActionMinW, textSize.x + kActionPad);
						break;
				}
				const Span span{x, width};
				optionSpans.push_back(span);

				if (op.type == OptionType::Label) {
					font.Draw(op.label, MakeVector2(x, kBtnY + (kTbH - textSize.y) * 0.5F), 1.0F,
					          textColor);
				} else { // Bool toggle or Action button
					bool hover = hovered(span, op.enabled, previousOptionHoverState, i);
					// An action holds no state, so it never draws as toggled.
					bool toggled = (op.type == OptionType::Bool) && op.bvalue;
					widgets::PaintButton(renderer, font, MakeVector2(x, kBtnY),
					                     MakeVector2(width, kTbH), op.label,
					                     MakeVector2(0.5F, 0.5F), "", MakeVector2(1.0F, 0.5F),
					                     op.enabled, hover, false, toggled, 1.0F);
				}
				x += width;
			}
		}
	} // namespace gui
} // namespace spades
