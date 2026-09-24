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
#include <cmath>
#include <utility>

#include <Gui/OverlayPaint.h>
#include <Gui/UIWidgetPainter.h>
#include <Client/IRenderer.h>
#include <Client/Fonts.h>
#include <Client/IFont.h>
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
		// A number box: its axis letter, the box itself, and the arrow column on
		// its right. The box is a fixed width so a row of them lines up and the
		// options after it hold still as the digits change.
		static const float kNumberLabelGap = 4.0F;
		static const float kNumberBoxW = 52.0F;
		static const float kNumberArrowW = 11.0F;
		static const float kNumberTextInset = 5.0F;
		// Half the arrow glyph, which is drawn as a triangle rather than an
		// image: at this size two of them stack inside the bar's button height.
		static const float kNumberArrowHalfW = 3.5F;
		static const float kNumberArrowHalfH = 2.0F;

		OptionBar::OptionBar(client::IAudioDevice* audioDevice) : sounds(audioDevice) {}

		void OptionBar::SetContent(const void* newOwner, std::vector<SubToolButton> buttons,
		                           std::vector<Option> newOptions) {
			owner = newOwner;
			subToolButtons = std::move(buttons);
			options = std::move(newOptions);
			// The host rebuilds the bar every frame. A number being typed into
			// belongs to the owner that offered it, and goes when that owner or
			// that option does — dropped rather than committed, since nobody is
			// left to take the value.
			if (editing && (owner != editingOwner || EditingIndex() < 0)) {
				editing = false;
				entry.End();
			}
		}

		int OptionBar::EditingIndex() const {
			if (!editing)
				return -1;
			for (size_t i = 0; i < options.size(); i++) {
				if (options[i].type == OptionType::Number && options[i].id == editingId)
					return int(i);
			}
			return -1;
		}

		void OptionBar::ReportNumber(const std::string& id, float value, bool committed) {
			if (OnNumberChanged)
				OnNumberChanged(id, value, committed);
		}

		void OptionBar::EndEditing(bool commit) {
			if (!editing)
				return;
			const std::string id = editingId; // the handler may rebuild the options
			float typed;
			// Text that is not a number is not a value to settle on, so it counts
			// as no edit at all rather than a guess at what was meant.
			const bool settle = commit && entry.Value(typed);
			editing = false;
			entry.End();
			// Either way a value is reported as settled: what was typed, or the
			// one the box started from. The second is what puts back a host that
			// has been showing each keystroke live — it has no other way to know
			// the edit is off.
			ReportNumber(id, settle ? typed : editingStartValue, true);
		}

		void OptionBar::BeginEditing(size_t i) {
			const Option& op = options[i];
			if (editing && editingId == op.id)
				return; // already in this box
			EndEditing(true);
			editing = true;
			editingOwner = owner;
			editingId = op.id;
			editingStartValue = op.value;
			entry.Begin(op.value, op.decimals);
		}

		void OptionBar::StepNumber(size_t i, float delta) {
			const Option& op = options[i];
			// Stepping settles whatever was being typed first, so the step always
			// works from a value the host has actually taken.
			EndEditing(true);
			const float scale = std::pow(10.0F, float(std::max(0, op.decimals)));
			const float stepped = std::round((op.value + delta) * scale) / scale;
			ReportNumber(op.id, stepped, true);
		}

		bool OptionBar::Click(const Vector2& p) {
			if (CurrentOwner && CurrentOwner() != drawnOwner) {
				// What is on screen belongs to an owner no longer current. The
				// edit belongs to that owner too, so it goes with it.
				EndEditing(true);
				return false;
			}
			// A press on a number box or its arrows, before anything else: the
			// box sits inside an option's span, so the generic pass below would
			// otherwise swallow it.
			for (size_t i = 0; i < numberSpans.size() && i < options.size(); i++) {
				if (options[i].type != OptionType::Number || !options[i].enabled)
					continue;
				const NumberSpans& ns = numberSpans[i];
				const float half = kTbH * 0.5F;
				if (OverlayInRect(p, ns.arrows.x, kBtnY, ns.arrows.width, half)) {
					StepNumber(i, options[i].step);
					sounds.Activate();
					return true;
				}
				if (OverlayInRect(p, ns.arrows.x, kBtnY + half, ns.arrows.width, half)) {
					StepNumber(i, -options[i].step);
					sounds.Activate();
					return true;
				}
				if (OverlayInRect(p, ns.box.x, kBtnY, ns.box.width, kTbH)) {
					BeginEditing(i);
					sounds.Activate();
					return true;
				}
			}
			// Anywhere else settles what was being typed — including a click over
			// the viewport, which the host also reports here.
			EndEditing(true);
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
					// A readout has nothing to click, and a number's own parts
					// were tested before this pass reached it.
					case OptionType::Label:
					case OptionType::Number: break;
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

		bool OptionBar::KeyEvent(const std::string& key) {
			if (!editing)
				return false;
			const int index = EditingIndex();
			if (index < 0) {
				EndEditing(false);
				return false;
			}
			if (key == "Enter") {
				EndEditing(true);
			} else if (key == "Escape") {
				EndEditing(false);
			} else if (key == "Up") {
				StepNumber(size_t(index), options[size_t(index)].step);
			} else if (key == "Down") {
				StepNumber(size_t(index), -options[size_t(index)].step);
			} else if (key == "BackSpace") {
				entry.Backspace();
			} else if (key == "Delete") {
				entry.Delete();
			} else if (key == "Left") {
				entry.MoveCaret(-1);
			} else if (key == "Right") {
				entry.MoveCaret(1);
			} else if (key == "Home") {
				entry.CaretToStart();
			} else if (key == "End") {
				entry.CaretToEnd();
			} else {
				// Every other key is swallowed while a box is being typed into,
				// so a tool hot key never fires on a letter meant for the text.
				return true;
			}
			// An edit that survived shows as it is typed; the host decides what a
			// value it has not settled on is worth.
			if (editing) {
				float value;
				if (entry.Value(value))
					ReportNumber(editingId, value, false);
			}
			return true;
		}

		void OptionBar::TextInputEvent(const std::string& text) {
			if (!editing)
				return;
			entry.Insert(text);
			float value;
			if (entry.Value(value))
				ReportNumber(editingId, value, false);
		}

		AABB2 OptionBar::EditingRect() const {
			const int index = EditingIndex();
			if (index < 0 || size_t(index) >= numberSpans.size())
				return AABB2();
			const Span& box = numberSpans[size_t(index)].box;
			return AABB2(box.x, kBtnY, box.width, kTbH);
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
			numberSpans.clear();
			numberSpans.resize(options.size());
			const int editingIndex = EditingIndex();

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
					case OptionType::Number:
						width = textSize.x + kNumberLabelGap + kNumberBoxW + kNumberArrowW;
						break;
				}
				const Span span{x, width};
				optionSpans.push_back(span);

				if (op.type == OptionType::Label) {
					font.Draw(op.label, MakeVector2(x, kBtnY + (kTbH - textSize.y) * 0.5F), 1.0F,
					          textColor);
				} else if (op.type == OptionType::Number) {
					DrawNumber(renderer, font, op, i, span, textSize, editingIndex == int(i),
					           menuActive, cursorPos);
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
		void OptionBar::DrawNumber(client::IRenderer& renderer, client::IFont& font,
		                           const Option& op, size_t i, const Span& span,
		                           const Vector2& labelSize, bool isEditing, bool menuActive,
		                           const Vector2& cursorPos) {
			const Vector4 labelColor = MakeVector4(0.75F, 0.75F, 0.75F, 1.0F);
			const Vector4 valueColor = MakeVector4(0.9F, 0.9F, 0.95F, 1.0F);
			const Vector4 arrowColor = MakeVector4(0.8F, 0.8F, 0.85F, 1.0F);
			const Vector4 disabledColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.3F);

			// The axis letter, then the box, then the two arrows sharing a column
			// on its right.
			font.Draw(op.label, MakeVector2(span.x, kBtnY + (kTbH - labelSize.y) * 0.5F), 1.0F,
			          op.enabled ? labelColor : disabledColor);
			const float boxX = span.x + labelSize.x + kNumberLabelGap;
			const float arrowX = boxX + kNumberBoxW;
			const float half = kTbH * 0.5F;

			NumberSpans& ns = numberSpans[i];
			ns.box = Span{boxX, kNumberBoxW};
			ns.arrows = Span{arrowX, kNumberArrowW};

			const bool boxHover = !menuActive && op.enabled &&
			                      OverlayInRect(cursorPos, boxX, kBtnY, kNumberBoxW, kTbH);
			widgets::PaintField(renderer, MakeVector2(boxX, kBtnY),
			                    MakeVector2(kNumberBoxW, kTbH), isEditing, boxHover);

			// While it is typed into the box shows the text as it stands, caret
			// and all; otherwise the value the tool last put there.
			const std::string shown =
			  isEditing ? entry.Text() : NumberEntry::Format(op.value, op.decimals);
			const Vector2 shownSize = font.Measure(shown);
			font.Draw(shown, MakeVector2(boxX + kNumberTextInset, kBtnY + (kTbH - shownSize.y) * 0.5F),
			          1.0F, op.enabled ? valueColor : disabledColor);
			if (isEditing) {
				const float caretX =
				  boxX + kNumberTextInset + font.Measure(shown.substr(0, size_t(entry.Caret()))).x;
				OverlayColorNP(renderer, valueColor);
				OverlayFillRect(renderer, caretX, kBtnY + 4.0F, 1.0F, kTbH - 8.0F);
			}

			// Triangles rather than an image: the bar draws everything else from
			// primitives, and these have to sit in half a button's height.
			auto arrow = [&](float centerY, bool up, bool hover) {
				const float cx = arrowX + kNumberArrowW * 0.5F;
				const float dy = up ? -kNumberArrowHalfH : kNumberArrowHalfH;
				const Vector2 points[3] = {MakeVector2(cx, centerY + dy),
				                           MakeVector2(cx - kNumberArrowHalfW, centerY - dy),
				                           MakeVector2(cx + kNumberArrowHalfW, centerY - dy)};
				Vector4 c = op.enabled ? arrowColor : disabledColor;
				if (op.enabled)
					c.w = hover ? 1.0F : 0.7F;
				OverlayFillConvexPolygon(renderer, points, 3, c);
			};
			const bool upHover = !menuActive && op.enabled &&
			                     OverlayInRect(cursorPos, arrowX, kBtnY, kNumberArrowW, half);
			const bool downHover =
			  !menuActive && op.enabled &&
			  OverlayInRect(cursorPos, arrowX, kBtnY + half, kNumberArrowW, half);
			arrow(kBtnY + half * 0.5F, true, upHover);
			arrow(kBtnY + half * 1.5F, false, downHover);
		}
	} // namespace gui
} // namespace spades
