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

#pragma once

#include <Gui/UI/Framework/UIElement.h>
#include <Gui/UI/Widgets/ButtonBase.h>

namespace spades {
	namespace client {
		class IImage;
		class IRenderer;
	}
	namespace gui {
		namespace ui {
			enum class ScrollBarOrientation { Horizontal, Vertical };

			/** Value model and change notification shared by scrollbars and sliders. */
			class ScrollBarBase : public UIElement {
			public:
				double minValue = 0.0;
				double maxValue = 100.0;
				double value = 0.0;
				double smallChange = 1.0;
				double largeChange = 20.0;
				EventHandler changed;

				ScrollBarBase(UIManager* manager) : UIElement(manager) {}

				void ScrollBy(double delta) { ScrollTo(value + delta); }
				void ScrollTo(double val);

				virtual void OnChanged();

				ScrollBarOrientation GetOrientation() const;
			};

			class ScrollBar;

			/** The draggable thumb of a `ScrollBar`. */
			class ScrollBarTrackBar : public UIElement {
				ScrollBar* scrollBar; // weak; owned as our parent
				bool dragging = false;
				double startValue = 0.0;
				float startCursorPos = 0.0F;
				bool hover = false;

				float GetCursorPos(Vector2 pos) const;

			public:
				ScrollBarTrackBar(ScrollBar* scrollBar);

				void MouseDown(MouseButton button, Vector2 clientPosition) override;
				void MouseMove(Vector2 clientPosition) override;
				void MouseUp(MouseButton button, Vector2 clientPosition) override;
				void MouseEnter() override;
				void MouseLeave() override;
				void Render() override;
			};

			/** Invisible repeat-button covering the track above/below the thumb. */
			class ScrollBarFill : public ButtonBase {
			public:
				ScrollBarFill(UIManager* manager);

				void PlayMouseEnterSound() override {} // suppress
				void PlayActivateSound() override {}   // suppress
				void Render() override {}              // nothing to draw
			};

			/** An arrow repeat-button at either end of a `ScrollBar`. */
			class ScrollBarButton : public ButtonBase {
				ScrollBar* scrollBar; // weak
				bool up;
				Handle<client::IImage> image;

			public:
				ScrollBarButton(ScrollBar* scrollBar, bool up);

				void PlayMouseEnterSound() override {} // suppress
				void PlayActivateSound() override {}   // suppress
				void Render() override;
			};

			/** A standard scrollbar with two arrow buttons and a draggable thumb. */
			class ScrollBar : public ScrollBarBase {
				ScrollBarTrackBar* trackBar;
				ScrollBarFill* fill1;
				ScrollBarFill* fill2;
				ScrollBarButton* button1;
				ScrollBarButton* button2;

				float buttonSize = 16.0F;

			public:
				ScrollBar(UIManager* manager);

				void OnChanged() override;
				void Layout();
				void OnResized() override;

				float GetLength() const;
				float GetTrackBarAreaLength() const { return GetLength() - buttonSize - buttonSize; }
				float GetTrackBarLength() const;
				float GetTrackBarMovementRange() const {
					return GetTrackBarAreaLength() - GetTrackBarLength();
				}
				float GetTrackBarPosition() const;

				void Render() override;
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
