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

#include <algorithm>

#include "DrawUtils.h"
#include "ScrollBar.h"
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		namespace ui {
			void ScrollBarBase::ScrollTo(double val) {
				val = Clamp(val, minValue, maxValue);
				if (val == value)
					return;
				value = val;
				OnChanged();
			}

			void ScrollBarBase::OnChanged() {
				if (changed)
					changed(*this);
			}

			ScrollBarOrientation ScrollBarBase::GetOrientation() const {
				if (size.x > size.y)
					return ScrollBarOrientation::Horizontal;
				else
					return ScrollBarOrientation::Vertical;
			}

			// -- ScrollBarTrackBar --

			ScrollBarTrackBar::ScrollBarTrackBar(ScrollBar* scrollBar)
			    : UIElement(&scrollBar->GetManager()), scrollBar(scrollBar) {
				isMouseInteractive = true;
			}

			float ScrollBarTrackBar::GetCursorPos(Vector2 pos) const {
				if (scrollBar->GetOrientation() == ScrollBarOrientation::Horizontal)
					return pos.x + position.x;
				else
					return pos.y + position.y;
			}

			void ScrollBarTrackBar::MouseDown(MouseButton button, Vector2 clientPosition) {
				if (button != MouseButton::Left)
					return;
				if (scrollBar->GetTrackBarMovementRange() < 0.0001F)
					return; // immobile

				dragging = true;
				startValue = scrollBar->value;
				startCursorPos = GetCursorPos(clientPosition);
			}

			void ScrollBarTrackBar::MouseMove(Vector2 clientPosition) {
				if (dragging) {
					double val = startValue;
					float delta = GetCursorPos(clientPosition) - startCursorPos;
					val += delta * (scrollBar->maxValue - scrollBar->minValue) /
					       double(scrollBar->GetTrackBarMovementRange());
					scrollBar->ScrollTo(val);
				}
			}

			void ScrollBarTrackBar::MouseUp(MouseButton button, Vector2 clientPosition) {
				(void)clientPosition;
				if (button != MouseButton::Left)
					return;
				dragging = false;
			}

			void ScrollBarTrackBar::MouseEnter() {
				hover = true;
				UIElement::MouseEnter();
			}

			void ScrollBarTrackBar::MouseLeave() {
				hover = false;
				UIElement::MouseLeave();
			}

			void ScrollBarTrackBar::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;

				if (scrollBar->GetOrientation() == ScrollBarOrientation::Horizontal) {
					pos.y += 4.0F;
					sz.y -= 8.0F;
				} else {
					pos.x += 4.0F;
					sz.x -= 8.0F;
				}

				if (dragging)
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.4F));
				else if (hover)
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.2F));
				else
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.1F));
				r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));
			}

			// -- ScrollBarFill --

			ScrollBarFill::ScrollBarFill(ScrollBarBase* scrollBar, bool up)
			    : ButtonBase(&scrollBar->GetManager()) {
				(void)up; // unused
				isMouseInteractive = true;
				repeat = true;
			}

			// -- ScrollBarButton --

			ScrollBarButton::ScrollBarButton(ScrollBar* scrollBar, bool up)
			    : ButtonBase(&scrollBar->GetManager()), scrollBar(scrollBar), up(up) {
				isMouseInteractive = true;
				repeat = true;
				image = GetManager().GetRenderer().RegisterImage("Gfx/UI/ScrollArrow.png");
			}

			void ScrollBarButton::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;
				pos += sz * 0.5F;
				float siz = image->GetWidth() * 0.5F;
				AABB2 srcRect(0.0F, 0.0F, image->GetWidth(), image->GetHeight());

				if (pressed && hover)
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.6F));
				else if (hover)
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.4F));
				else
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.2F));

				if (scrollBar->GetOrientation() == ScrollBarOrientation::Horizontal) {
					if (up) {
						r.DrawImage(image, MakeVector2(pos.x + siz, pos.y - siz),
						            MakeVector2(pos.x + siz, pos.y + siz),
						            MakeVector2(pos.x - siz, pos.y - siz), srcRect);
					} else {
						r.DrawImage(image, MakeVector2(pos.x - siz, pos.y + siz),
						            MakeVector2(pos.x - siz, pos.y - siz),
						            MakeVector2(pos.x + siz, pos.y + siz), srcRect);
					}
				} else {
					if (up) {
						r.DrawImage(image, MakeVector2(pos.x - siz, pos.y + siz),
						            MakeVector2(pos.x + siz, pos.y + siz),
						            MakeVector2(pos.x - siz, pos.y - siz), srcRect);
					} else {
						r.DrawImage(image, MakeVector2(pos.x - siz, pos.y - siz),
						            MakeVector2(pos.x + siz, pos.y - siz),
						            MakeVector2(pos.x - siz, pos.y + siz), srcRect);
					}
				}
			}

			// -- ScrollBar --

			ScrollBar::ScrollBar(UIManager* manager) : ScrollBarBase(manager) {
				Handle<ScrollBarTrackBar> tb = Handle<ScrollBarTrackBar>::New(this);
				trackBar = tb.GetPointerOrNull();
				AddChild(trackBar);

				Handle<ScrollBarFill> f1 = Handle<ScrollBarFill>::New(this, false);
				fill1 = f1.GetPointerOrNull();
				fill1->activated = [this](UIElement&) { ScrollBy(-largeChange); };
				AddChild(fill1);
				Handle<ScrollBarFill> f2 = Handle<ScrollBarFill>::New(this, true);
				fill2 = f2.GetPointerOrNull();
				fill2->activated = [this](UIElement&) { ScrollBy(largeChange); };
				AddChild(fill2);

				Handle<ScrollBarButton> b1 = Handle<ScrollBarButton>::New(this, false);
				button1 = b1.GetPointerOrNull();
				button1->activated = [this](UIElement&) { ScrollBy(-smallChange); };
				AddChild(button1);
				Handle<ScrollBarButton> b2 = Handle<ScrollBarButton>::New(this, true);
				button2 = b2.GetPointerOrNull();
				button2->activated = [this](UIElement&) { ScrollBy(smallChange); };
				AddChild(button2);
			}

			void ScrollBar::OnChanged() {
				Layout();
				ScrollBarBase::OnChanged();
				Layout();
			}

			void ScrollBar::Layout() {
				Vector2 sz = size;
				float pos = GetTrackBarPosition();
				float len = GetTrackBarLength();
				if (GetOrientation() == ScrollBarOrientation::Horizontal) {
					button1->SetBounds(AABB2(0, 0, buttonSize, sz.y));
					button2->SetBounds(AABB2(sz.x - buttonSize, 0, buttonSize, sz.y));
					fill1->SetBounds(AABB2(buttonSize, 0, pos - buttonSize, sz.y));
					fill2->SetBounds(
					    AABB2(pos + len, 0, sz.x - buttonSize - pos - len, sz.y));
					trackBar->SetBounds(AABB2(pos, 0, len, sz.y));
				} else {
					button1->SetBounds(AABB2(0, 0, sz.x, buttonSize));
					button2->SetBounds(AABB2(0, sz.y - buttonSize, sz.x, buttonSize));
					fill1->SetBounds(AABB2(0, buttonSize, sz.x, pos - buttonSize));
					fill2->SetBounds(
					    AABB2(0, pos + len, sz.x, sz.y - buttonSize - pos - len));
					trackBar->SetBounds(AABB2(0, pos, sz.x, len));
				}
			}

			void ScrollBar::OnResized() {
				Layout();
				UIElement::OnResized();
			}

			float ScrollBar::GetLength() const {
				if (GetOrientation() == ScrollBarOrientation::Horizontal)
					return size.x;
				else
					return size.y;
			}

			float ScrollBar::GetTrackBarLength() const {
				return std::max(
				    static_cast<float>(GetTrackBarAreaLength() *
				                       (largeChange / (maxValue - minValue + largeChange))),
				    16.0F);
			}

			float ScrollBar::GetTrackBarPosition() const {
				if (maxValue == minValue)
					return buttonSize;
				return static_cast<float>((value - minValue) / (maxValue - minValue) *
				                          GetTrackBarMovementRange()) +
				       buttonSize;
			}

			void ScrollBar::Render() {
				if (GetTrackBarMovementRange() < 0.0001F)
					return; // immobile

				Layout();
				UIElement::Render();
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
