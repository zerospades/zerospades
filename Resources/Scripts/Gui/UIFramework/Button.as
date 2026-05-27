/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "UIFramework.as"

namespace spades {
	namespace ui {

		class ButtonBase : UIElement {
			bool Pressed = false;
			bool Hover = false;
			bool Toggled = false;
			bool Toggle = false;
			bool Repeat = false;
			bool ActivateOnMouseDown = false;

			EventHandler@ Activated;
			EventHandler@ DoubleClicked;
			EventHandler@ RightClicked;
			string Caption;
			string ActivateHotKey;

			private Timer@ repeatTimer;

			// for double click detection
			private float lastActivate = -1.0F;
			private Vector2 lastActivatePosition = Vector2();

			ButtonBase(UIManager@ manager) {
				super(manager);
				IsMouseInteractive = true;
				@repeatTimer = Timer(Manager);
				@repeatTimer.Tick = TimerTickEventHandler(this.RepeatTimerFired);
			}

			void PlayMouseEnterSound() { Manager.PlaySound("Sounds/Feedback/Limbo/Hover.opus"); }
			void PlayActivateSound() { Manager.PlaySound("Sounds/Feedback/Limbo/Select.opus"); }

			void OnActivated() {
				if (Activated !is null)
					Activated(this);
			}

			void OnDoubleClicked() {
				if (DoubleClicked !is null)
					DoubleClicked(this);
			}

			void OnRightClicked() {
				if (RightClicked !is null)
					RightClicked(this);
			}

			private void RepeatTimerFired(Timer@ timer) {
				OnActivated();
				timer.Interval = 0.1F;
			}

			void MouseDown(MouseButton button, Vector2 clientPosition) {
				if (button != spades::ui::MouseButton::LeftMouseButton and
					button != spades::ui::MouseButton::RightMouseButton) {
					return;
				}

				PlayActivateSound();
				if (button == spades::ui::MouseButton::RightMouseButton) {
					OnRightClicked();
					return;
				}

				Pressed = true;
				Hover = true;

				if (Repeat or ActivateOnMouseDown) {
					OnActivated();
					if (Repeat) {
						repeatTimer.Interval = 0.3F;
						repeatTimer.Start();
					}
				}
			}
			void MouseMove(Vector2 clientPosition) {
				if (Pressed) {
					bool newHover = AABB2(Vector2(0.0F, 0.0F), Size).Contains(clientPosition);
					if (newHover != Hover) {
						if (Repeat) {
							if (newHover) {
								OnActivated();
								repeatTimer.Interval = 0.3F;
								repeatTimer.Start();
							} else {
								repeatTimer.Stop();
							}
						}

						Hover = newHover;
					}
				}
			}
			void MouseUp(MouseButton button, Vector2 clientPosition) {
				if (button != spades::ui::MouseButton::LeftMouseButton and
					button != spades::ui::MouseButton::RightMouseButton) {
					return;
				}

				if (Pressed) {
					Pressed = false;
					if (Hover and not(Repeat or ActivateOnMouseDown)) {
						if (Toggle)
							Toggled = !Toggled;

						OnActivated();
						if (Manager.Time - lastActivate < 0.35F and
							(clientPosition - lastActivatePosition).ManhattanLength < 10.0F) {
							OnDoubleClicked();
						}

						lastActivate = Manager.Time;
						lastActivatePosition = clientPosition;
					}

					if (Repeat and Hover)
						repeatTimer.Stop();
				}
			}
			void MouseEnter() {
				Hover = true;
				if (not Pressed)
					PlayMouseEnterSound();
				UIElement::MouseEnter();
			}
			void MouseLeave() {
				Hover = false;
				UIElement::MouseLeave();
			}

			void KeyDown(string key) {
				if (key == " ")
					OnActivated();
				UIElement::KeyDown(key);
			}
			void KeyUp(string key) { UIElement::KeyUp(key); }

			void HotKey(string key) {
				if (key == ActivateHotKey)
					OnActivated();
			}
		}

		class SimpleButton : spades::ui::Button {
			Vector4 TextColor = Vector4(1, 1, 1, 1);
			Vector4 DisabledTextColor = Vector4(1.0F, 1.0F, 1.0F, 0.4F);

			SimpleButton(spades::ui::UIManager@ manager) { super(manager); }

			void Render() {
				Renderer@ r = Manager.Renderer;
				Vector2 pos = ScreenPosition;
				Vector2 size = Size;

				Vector4 color = IsEnabled ? TextColor : DisabledTextColor;

				if (Toggled or (Pressed and Hover))
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.2F);
				else if (Hover)
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.12F);
				else
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.07F * color.w);
				r.DrawImage(null, AABB2(pos.x, pos.y, size.x, size.y));

				if (Toggled or (Pressed and Hover))
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.1F);
				else if (Hover)
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.07F);
				else
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.03F * color.w);
				r.DrawOutlinedRect(pos.x, pos.y, pos.x + size.x, pos.y + size.y);

				pos += Vector2(4.0F, 4.0F);
				size -= Vector2(8.0F, 8.0F);

				Vector2 txtSize = Font.Measure(Caption);
				Vector2 txtPos = pos + (size - txtSize) * Alignment;

				Font.DrawShadow(Caption, txtPos, 1.0F, color, Vector4(0, 0, 0, 0.4F * color.w));
			}
		}

		class CheckBox : spades::ui::Button {
			CheckBox(spades::ui::UIManager @manager) {
				super(manager);
				this.Toggle = true;
			}
			void Render() {
				Renderer@ r = Manager.Renderer;
				Vector2 pos = ScreenPosition;
				Vector2 size = Size;
				Image@ img = r.RegisterImage("Gfx/UI/CheckBox.png");

				if (Pressed and Hover)
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.2F);
				else if (Hover)
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.12F);
				else
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.0F);
				r.DrawImage(null, AABB2(pos.x, pos.y, size.x, size.y));

				Vector2 txtSize = Font.Measure(Caption);
				Font.DrawShadow(Caption, pos + (size - txtSize)
					* Vector2(0.0F, 0.5F) + Vector2(16.0F, 0.0F),
					1.0F, Vector4(1, 1, 1, 1), Vector4(0, 0, 0, 0.2F));

				r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, Toggled ? 0.9F : 0.6F);
				r.DrawImage(img, AABB2(pos.x, pos.y + (size.y - 16.0F) * 0.5F, 16.0F, 16.0F),
								   AABB2(Toggled ? 16.0F : 0.0F, 0.0F, 16.0F, 16.0F));
			}
		}

		class RadioButton : spades::ui::Button {
			string GroupName;

			RadioButton(spades::ui::UIManager @manager) {
				super(manager);
				this.Toggle = true;
			}
			void Check() {
				this.Toggled = true;

				// uncheck others
				if (GroupName.length > 0) {
					UIElement @[] @children = this.Parent.GetChildren();
					for (uint i = 0, count = children.length; i < children.length; i++) {
						RadioButton@ btn = cast<RadioButton>(children[i]);
						if (btn is this)
							continue;
						if (btn !is null) {
							if (GroupName == btn.GroupName)
								btn.Toggled = false;
						}
					}
				}
			}
			void OnActivated() {
				Check();

				Button::OnActivated();
			}
			void Render() {
				Renderer@ r = Manager.Renderer;
				Vector2 pos = ScreenPosition;
				Vector2 size = Size;

				if (not this.Enable)
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.07F);

				if (Toggled or (Pressed and Hover))
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.2F);
				else if (Hover)
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.12F);
				else
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.07F);
				r.DrawImage(null, AABB2(pos.x, pos.y, size.x, size.y));

				if (not this.Enable)
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.03F);

				if (Toggled or (Pressed and Hover))
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.06F);
				else if (Hover)
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.04F);
				else
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.02F);
				r.DrawOutlinedRect(pos.x, pos.y, pos.x + size.x, pos.y + size.y);

				Vector2 txtSize = Font.Measure(Caption);
				Font.DrawShadow(Caption, pos + (size - txtSize) * 0.5F + Vector2(8.0F, 0.0F), 1.0F,
					Vector4(1, 1, 1, (Toggled and this.Enable) ? 1.0F : 0.4F),
					Vector4(0, 0, 0, 0.4F));

				if (Toggled) {
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, (Toggled and this.Enable) ? 0.6F : 0.3F);
					r.DrawImage(null, AABB2(pos.x + 4.0F, pos.y + (size.y - 8.0F) * 0.5F, 8.0F, 8.0F));
				}
			}
		}

		class Button : ButtonBase {
			Vector2 Alignment = Vector2(0.5F, 0.5F);

			string HotKeyText;
			Vector2 HotKeyTextAlignment = Vector2(1.0F, 0.5F);

			Button(UIManager@ manager) {
				super(manager);
			}

			void Render() {
				Renderer@ r = Manager.Renderer;
				Vector2 pos = ScreenPosition;
				Vector2 size = Size;

				Vector4 color = Vector4(0.2F, 0.2F, 0.2F, 0.5F);

				if (IsEnabled) {
					if (Toggled or (Pressed and Hover))
						color = Vector4(0.7F, 0.7F, 0.7F, 0.9F);
					else if (Hover)
						color = Vector4(0.4F, 0.4F, 0.4F, 0.7F);
				} else {
					color.w *= 0.5F;
				}

				r.ColorNP = color;
				r.DrawFilledRect(pos.x + 1, pos.y + 1, pos.x + size.x - 1, pos.y + size.y - 1);

				r.ColorNP = Vector4(0.0F, 0.0F, 0.0F, color.w);
				r.DrawOutlinedRect(pos.x + 1, pos.y + 1, pos.x + size.x - 1, pos.y + size.y - 1);

				Font@ font = this.Font;
				string text = this.Caption;

				pos += Vector2(8.0F, 8.0F);
				size -= Vector2(16.0F, 16.0F);

				if (HotKeyText.length > 0)
					Alignment = Vector2(0.0F, 0.5F);

				Vector2 txtSize = font.Measure(text);
				Vector2 txtPos = pos + (size - txtSize) * Alignment;
				font.DrawShadow(text, txtPos, 1.0F,
					Vector4(1.0F, 1.0F, 1.0F, IsEnabled ? 1.0F : 0.5F),
					Vector4(0.0F, 0.0F, 0.0F, IsEnabled ? 0.4F : 0.1F));

				// TODO: use "FontManager::SmallFont" for this
				txtSize = font.Measure(HotKeyText);
				txtPos = pos + (size - txtSize) * HotKeyTextAlignment;
				font.DrawShadow(HotKeyText, txtPos, 1.0F,
					Vector4(1.0F, 1.0F, 1.0F, IsEnabled ? 0.6F : 0.3F),
					Vector4(0.0F, 0.0F, 0.0F, IsEnabled ? 0.1F : 0.05F));
			}
		}
	}
}
