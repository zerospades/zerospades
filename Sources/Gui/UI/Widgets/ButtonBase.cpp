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

#include "ButtonBase.h"
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		namespace ui {
			ButtonBase::ButtonBase(UIManager* manager) : UIElement(manager) {
				isMouseInteractive = true;
				repeatTimer = Handle<Timer>::New(manager);
				repeatTimer->tick = [this](Timer& timer) {
					OnActivated();
					timer.interval = 0.1F;
				};
			}

			ButtonBase::~ButtonBase() { repeatTimer->Stop(); }

			void ButtonBase::PlayMouseEnterSound() {
				GetManager().PlaySound("Sounds/Feedback/Limbo/Hover.opus");
			}

			void ButtonBase::PlayActivateSound() {
				GetManager().PlaySound("Sounds/Feedback/Limbo/Select.opus");
			}

			void ButtonBase::OnActivated() {
				if (activated)
					activated(*this);
			}

			void ButtonBase::OnDoubleClicked() {
				if (doubleClicked)
					doubleClicked(*this);
			}

			void ButtonBase::OnRightClicked() {
				if (rightClicked)
					rightClicked(*this);
			}

			void ButtonBase::MouseDown(MouseButton button, Vector2 clientPosition) {
				(void)clientPosition;
				if (button != MouseButton::Left && button != MouseButton::Right)
					return;

				PlayActivateSound();
				if (button == MouseButton::Right) {
					OnRightClicked();
					return;
				}

				pressed = true;
				hover = true;

				if (repeat || activateOnMouseDown) {
					OnActivated();
					if (repeat) {
						repeatTimer->interval = 0.3F;
						repeatTimer->Start();
					}
				}
			}

			void ButtonBase::MouseMove(Vector2 clientPosition) {
				if (pressed) {
					bool newHover = AABB2(MakeVector2(0, 0), size).Contains(clientPosition);
					if (newHover != hover) {
						if (repeat) {
							if (newHover) {
								OnActivated();
								repeatTimer->interval = 0.3F;
								repeatTimer->Start();
							} else {
								repeatTimer->Stop();
							}
						}

						hover = newHover;
					}
				}
			}

			void ButtonBase::MouseUp(MouseButton button, Vector2 clientPosition) {
				if (button != MouseButton::Left && button != MouseButton::Right)
					return;

				if (pressed) {
					pressed = false;
					if (hover && !(repeat || activateOnMouseDown)) {
						if (toggle)
							toggled = !toggled;

						OnActivated();
						if (GetManager().time - lastActivate < 0.35F &&
						    (clientPosition - lastActivatePosition).GetManhattanLength() < 10.0F) {
							OnDoubleClicked();
						}

						lastActivate = GetManager().time;
						lastActivatePosition = clientPosition;
					}

					if (repeat && hover)
						repeatTimer->Stop();
				}
			}

			void ButtonBase::MouseEnter() {
				hover = true;
				if (!pressed)
					PlayMouseEnterSound();
				UIElement::MouseEnter();
			}

			void ButtonBase::MouseLeave() {
				hover = false;
				if (pressed && repeat)
					repeatTimer->Stop();
				UIElement::MouseLeave();
			}

			void ButtonBase::KeyDown(const std::string& key) {
				if (key == " ")
					OnActivated();
				UIElement::KeyDown(key);
			}

			void ButtonBase::KeyUp(const std::string& key) { UIElement::KeyUp(key); }

			void ButtonBase::HotKey(const std::string& key) {
				if (key == activateHotKey)
					OnActivated();
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
