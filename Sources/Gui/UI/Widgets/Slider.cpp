/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "DrawUtils.h"
#include "Slider.h"
#include <Client/IRenderer.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		namespace ui {
			SliderKnob::SliderKnob(Slider* slider)
			    : UIElement(&slider->GetManager()), slider(slider) {
				isMouseInteractive = true;
			}

			void SliderKnob::PlayMouseEnterSound() {
				GetManager().PlaySound("Sounds/Feedback/Limbo/Hover.opus");
			}
			void SliderKnob::PlayActivateSound() {
				GetManager().PlaySound("Sounds/Feedback/Limbo/Select.opus");
			}

			void SliderKnob::MouseDown(MouseButton button, Vector2 clientPosition) {
				if (button != MouseButton::Left)
					return;
				PlayActivateSound();
				dragging = true;
				startValue = slider->value;
				startCursorPos = GetCursorPos(clientPosition);
			}

			void SliderKnob::MouseMove(Vector2 clientPosition) {
				if (dragging) {
					double val = startValue;
					float delta = GetCursorPos(clientPosition) - startCursorPos;
					val += delta * (slider->maxValue - slider->minValue) /
					       double(slider->GetTrackBarMovementRange());
					slider->ScrollTo(val);
				}
			}

			void SliderKnob::MouseUp(MouseButton button, Vector2 clientPosition) {
				(void)clientPosition;
				if (button != MouseButton::Left)
					return;
				dragging = false;
			}

			void SliderKnob::MouseEnter() {
				hover = true;
				if (!dragging)
					PlayMouseEnterSound();
				UIElement::MouseEnter();
			}

			void SliderKnob::MouseLeave() {
				hover = false;
				UIElement::MouseLeave();
			}

			void SliderKnob::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;

				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, IsEnabled() ? 0.07F : 0.035F));
				r.DrawFilledRect(pos.x + 4, pos.y + 1, pos.x + sz.x - 4, pos.y + sz.y - 1);

				if (dragging && hover)
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, IsEnabled() ? 0.3F : 0.15F));
				else if (hover)
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, IsEnabled() ? 0.2F : 0.1F));
				else
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, IsEnabled() ? 0.1F : 0.05F));
				r.DrawOutlinedRect(pos.x + 4, pos.y + 1, pos.x + sz.x - 4, pos.y + sz.y - 1);
			}

			// -- Slider --

			Slider::Slider(UIManager* manager) : ScrollBarBase(manager) {
				Handle<SliderKnob> k = Handle<SliderKnob>::New(this);
				knob = k.GetPointerOrNull();
				AddChild(knob);

				Handle<ScrollBarFill> f1 = Handle<ScrollBarFill>::New(this);
				fill1 = f1.GetPointerOrNull();
				fill1->activated = [this](UIElement&) { ScrollBy(-largeChange); };
				AddChild(fill1);
				Handle<ScrollBarFill> f2 = Handle<ScrollBarFill>::New(this);
				fill2 = f2.GetPointerOrNull();
				fill2->activated = [this](UIElement&) { ScrollBy(largeChange); };
				AddChild(fill2);
			}

			void Slider::OnChanged() {
				Layout();
				ScrollBarBase::OnChanged();
				Layout();
			}

			void Slider::Layout() {
				Vector2 sz = size;
				float pos = GetTrackBarPosition();
				float len = GetTrackBarLength();
				fill1->SetBounds(AABB2(0, 0, pos, sz.y));
				fill2->SetBounds(AABB2(pos + len, 0, sz.x - pos - len, sz.y));
				knob->SetBounds(AABB2(pos, 0, len, sz.y));
			}

			void Slider::OnResized() {
				Layout();
				UIElement::OnResized();
			}

			float Slider::GetLength() const {
				if (GetOrientation() == ScrollBarOrientation::Horizontal)
					return size.x;
				else
					return size.y;
			}

			float Slider::GetTrackBarPosition() const {
				return static_cast<float>((value - minValue) / (maxValue - minValue) *
				                          GetTrackBarMovementRange());
			}

			void Slider::Render() {
				Layout();

				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;

				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, IsEnabled() ? 0.07F : 0.035F));
				r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));

				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, IsEnabled() ? 0.04F : 0.02F));
				r.DrawOutlinedRect(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);

				UIElement::Render();
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
