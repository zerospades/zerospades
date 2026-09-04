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

#include <string>

#include <Gui/UI/Framework/Timer.h>
#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		namespace ui {
			/**
			 * Common behavior for clickable controls: press/hover tracking, optional
			 * toggle and auto-repeat, double-click and right-click detection, and hot
			 * keys. Rendering is left to subclasses.
			 */
			class ButtonBase : public UIElement {
				Handle<Timer> repeatTimer;

				// for double click detection
				float lastActivate = -1.0F;
				Vector2 lastActivatePosition;

			public:
				bool pressed = false;
				bool hover = false;
				bool toggled = false;
				bool toggle = false;
				bool repeat = false;
				bool activateOnMouseDown = false;

				EventHandler activated;
				EventHandler doubleClicked;
				EventHandler rightClicked;
				std::string caption;
				std::string activateHotKey;

				ButtonBase(UIManager* manager);
				~ButtonBase();

				virtual void PlayMouseEnterSound();
				virtual void PlayActivateSound();

				virtual void OnActivated();
				virtual void OnDoubleClicked();
				virtual void OnRightClicked();

				void MouseDown(MouseButton button, Vector2 clientPosition) override;
				void MouseMove(Vector2 clientPosition) override;
				void MouseUp(MouseButton button, Vector2 clientPosition) override;
				void MouseEnter() override;
				void MouseLeave() override;

				void KeyDown(const std::string& key) override;
				void KeyUp(const std::string& key) override;
				void HotKey(const std::string& key) override;
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
