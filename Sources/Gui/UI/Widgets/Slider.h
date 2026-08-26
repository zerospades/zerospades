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

#include <Gui/UI/Widgets/ScrollBar.h>

namespace spades {
	namespace gui {
		namespace ui {
			class Slider;

			/** The draggable knob of a `Slider`. */
			class SliderKnob : public UIElement {
				Slider* slider; // weak; owned as our parent
				double startValue = 0.0;
				float startCursorPos = 0.0F;
				bool dragging = false;
				bool hover = false;

				float GetCursorPos(Vector2 pos) const { return pos.x + position.x; }

			public:
				SliderKnob(Slider* slider);

				void PlayMouseEnterSound();
				void PlayActivateSound();

				void MouseDown(MouseButton button, Vector2 clientPosition) override;
				void MouseMove(Vector2 clientPosition) override;
				void MouseUp(MouseButton button, Vector2 clientPosition) override;
				void MouseEnter() override;
				void MouseLeave() override;
				void Render() override;
			};

			/** A horizontal value slider with a fixed-size knob. */
			class Slider : public ScrollBarBase {
				SliderKnob* knob;
				ScrollBarFill* fill1;
				ScrollBarFill* fill2;

			public:
				Slider(UIManager* manager);

				void OnChanged() override;
				void Layout();
				void OnResized() override;

				float GetLength() const;
				float GetTrackBarAreaLength() const { return GetLength(); }
				float GetTrackBarLength() const { return 16.0F; }
				float GetTrackBarMovementRange() const {
					return GetTrackBarAreaLength() - GetTrackBarLength();
				}
				float GetTrackBarPosition() const;

				void Render() override;
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
