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

#include "Cursor.h"
#include "TextUtils.h"
#include "UIElement.h"
#include "UIManager.h"
#include <Client/IAudioChunk.h>
#include <Client/IAudioDevice.h>
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Core/Debug.h>
#include <Imports/SDL.h>

namespace spades {
	namespace gui {
		namespace ui {
			UIManager::UIManager(client::IRenderer* renderer, client::IAudioDevice* audioDevice)
			    : renderer(renderer), audioDevice(audioDevice) {
				screenWidth = this->renderer->ScreenWidth();
				screenHeight = this->renderer->ScreenHeight();

				// Screens lay themselves out from this extent, so a bad one produces a UI
				// that is built but invisible. Say so rather than failing silently.
				if (screenWidth <= 0.0F || screenHeight <= 0.0F)
					SPLog("[!] UI created with an unusable screen extent (%gx%g)",
					      screenWidth, screenHeight);

				rootElement = Handle<UIElement>::New(this);
				rootElement->size = MakeVector2(screenWidth, screenHeight);

				Handle<client::IImage> cursorImage =
				    this->renderer->RegisterImage("Gfx/UI/Cursor.png");
				defaultCursor = Handle<Cursor>::New(this, cursorImage.GetPointerOrNull(),
				                                     MakeVector2(8.0F, 8.0F));
				mouseCursorPosition = MakeVector2(screenWidth * 0.5F, screenHeight * 0.5F);

				keyRepeater.handler = [this](const std::string& key) { HandleKeyInner(key); };
				charRepeater.handler = [this](const std::string& chr) { HandleCharInner(chr); };
			}

			UIManager::~UIManager() {}

			MouseButton UIManager::TranslateMouseButton(const std::string& key) {
				if (key == "LeftMouseButton")
					return MouseButton::Left;
				else if (key == "RightMouseButton")
					return MouseButton::Right;
				else if (key == "MiddleMouseButton")
					return MouseButton::Middle;
				else if (key == "MouseButton4")
					return MouseButton::Button4;
				else if (key == "MouseButton5")
					return MouseButton::Button5;
				else
					return MouseButton::None;
			}

			void UIManager::DiscardElement(UIElement* element) {
				if (activeElement && activeElement->IsSameOrDescendantOf(element))
					activeElement = nullptr;
				if (mouseCapturedElement && mouseCapturedElement->IsSameOrDescendantOf(element))
					mouseCapturedElement = nullptr;
				if (mouseHoverElement && mouseHoverElement->IsSameOrDescendantOf(element))
					mouseHoverElement = nullptr;
			}

			UIElement* UIManager::GetMouseActiveElement() {
				if (mouseCapturedElement)
					return mouseCapturedElement.GetPointerOrNull();
				if (!rootElement->enable)
					return nullptr;
				return rootElement->MouseHitTest(mouseCursorPosition);
			}

			Cursor* UIManager::GetCurrentCursor() {
				UIElement* e = GetMouseActiveElement();
				if (!e)
					return defaultCursor.GetPointerOrNull();
				return e->GetCursor();
			}

			void UIManager::WheelEvent(float x, float y) {
				(void)x;
				UIElement* e = GetMouseActiveElement();
				if (e)
					e->MouseWheel(y);
			}

			void UIManager::MouseEventDone() {
				UIElement* e = GetMouseActiveElement();
				if (e)
					e->MouseMove(e->ScreenToClient(mouseCursorPosition));

				// check for mouse enter/leave
				if (!e)
					e = rootElement->MouseHitTest(mouseCursorPosition);
				if (e != mouseHoverElement.GetPointerOrNull()) {
					if (mouseHoverElement)
						mouseHoverElement->MouseLeave();
					mouseHoverElement = e;
					if (e)
						e->MouseEnter();
				}
			}

			void UIManager::MouseEvent(float x, float y) {
				mouseCursorPosition.x = Clamp(x, 0.0F, screenWidth);
				mouseCursorPosition.y = Clamp(y, 0.0F, screenHeight);
				MouseEventDone();
			}

			void UIManager::KeyPanic() {
				keyRepeater.KeyUp();
				charRepeater.KeyUp();
				editingText.clear();
				editingStart = 0;
				editingLength = 0;
				isShiftPressed = false;
				isControlPressed = false;
				isAltPressed = false;
				isMetaPressed = false;
			}

			void UIManager::KeyEvent(const std::string& key, bool down) {
				if (key.empty()) {
					if (!down) {
						keyRepeater.KeyUp();
						charRepeater.KeyUp();
					}
					return;
				}
				if (key == "Shift")
					isShiftPressed = down;
				if (key == "Control")
					isControlPressed = down;
				if (key == "Alt")
					isAltPressed = down;
				if (key == "Meta")
					isMetaPressed = down;
				if (key == "WheelUp") {
					UIElement* e = GetMouseActiveElement();
					if (e)
						e->MouseWheel(-1.0F);
					return;
				}
				if (key == "WheelDown") {
					UIElement* e = GetMouseActiveElement();
					if (e)
						e->MouseWheel(1.0F);
					return;
				}

				MouseButton mb = TranslateMouseButton(key);
				if (mb != MouseButton::None) {
					UIElement* e = GetMouseActiveElement();
					if (e) {
						// keep the target alive across dispatch: an activation handler
						// may detach and release this element mid-call (e.g. a drop-down
						// list closes itself), which the AngelScript GC used to tolerate
						Handle<UIElement> keep(e);
						if (down) {
							mouseCapturedElement = e;
							if (e->acceptsFocus)
								activeElement = e; // give keyboard focus, too
							e->MouseDown(mb, e->ScreenToClient(mouseCursorPosition));
						} else {
							mouseCapturedElement = nullptr;
							e->MouseUp(mb, e->ScreenToClient(mouseCursorPosition));
							MouseEventDone();
						}
					}
					return;
				}

				if (down) {
					HandleKeyInner(key);
					keyRepeater.KeyDown(key);
				} else {
					keyRepeater.KeyUp();
					charRepeater.KeyUp();
					UIElement* e = activeElement.GetPointerOrNull();
					if (e)
						e->KeyUp(key);
				}
			}

			void UIManager::TextInputEvent(const std::string& chr) {
				charRepeater.KeyDown(chr);
				HandleCharInner(chr);
			}

			void UIManager::TextEditingEvent(const std::string& chr, int start, int len) {
				editingText = chr;
				editingStart = GetByteIndexForString(chr, start);
				editingLength = GetByteIndexForString(chr, len, editingStart) - editingStart;
			}

			bool UIManager::AcceptsTextInput() {
				if (!activeElement) {
					editingText.clear();
					editingStart = 0;
					editingLength = 0;
				}
				return static_cast<bool>(activeElement);
			}

			AABB2 UIManager::GetTextInputRect() {
				UIElement* e = activeElement.GetPointerOrNull();
				if (e) {
					AABB2 rt = e->GetTextInputRect();
					Vector2 off = e->GetScreenPosition();
					rt.min += off;
					rt.max += off;
					return rt;
				}
				return AABB2();
			}

			void UIManager::HandleKeyInner(const std::string& key) {
				UIElement* e = activeElement.GetPointerOrNull();
				if (!editingText.empty()) {
					// text is being composed by the IME; ignore keys that would
					// conflict with the composition
					if (key == "Escape" || key == "BackSpace" || key == "Left" ||
					    key == "Right" || key == "Space" || key == "Enter" || key == "Up" ||
					    key == "Down" || key == "Tab") {
						return;
					}
				}

				if (e)
					e->KeyDown(key);
				else
					ProcessHotKey(key);
			}

			void UIManager::HandleCharInner(const std::string& chr) {
				UIElement* e = activeElement.GetPointerOrNull();
				if (e)
					e->KeyPress(chr);
			}

			void UIManager::ProcessHotKey(const std::string& key) {
				if (rootElement->visible)
					rootElement->HotKey(key);
			}

			void UIManager::AddTimer(Timer* timer) { timers.emplace_back(timer); }

			void UIManager::RemoveTimer(Timer* timer) {
				for (auto it = timers.begin(); it != timers.end(); ++it) {
					if (it->GetPointerOrNull() == timer) {
						timers.erase(it);
						break;
					}
				}
			}

			void UIManager::PlaySound(const std::string& filename) {
				if (!audioDevice)
					return;
				Handle<client::IAudioChunk> chunk(audioDevice->RegisterSound(filename.c_str()));
				audioDevice->PlayLocal(chunk.GetPointerOrNull(), client::AudioParam());
			}

			void UIManager::RunFrame(float dt) {
				// The window can be resized after this manager was built (fullscreen and
				// DPI transitions do not all complete synchronously), so track the
				// renderer rather than trusting the extent captured at construction.
				// Screens that computed absolute positions when they were built keep
				// them, but hit testing and cursor clamping stay correct.
				float sw = renderer->ScreenWidth();
				float sh = renderer->ScreenHeight();
				if (sw != screenWidth || sh != screenHeight) {
					SPLog("UI screen extent changed from %gx%g to %gx%g", screenWidth,
					      screenHeight, sw, sh);
					screenWidth = sw;
					screenHeight = sh;
					rootElement->size = MakeVector2(screenWidth, screenHeight);
				}

				if (activeElement &&
				    !(activeElement->IsVisible() && activeElement->IsEnabled())) {
					activeElement = nullptr;
				}

				// a timer may add or remove timers while ticking, so iterate a snapshot
				std::vector<Handle<Timer>> snapshot(timers);
				for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it)
					(*it)->RunFrame(dt);

				keyRepeater.RunFrame(dt);
				charRepeater.RunFrame(dt);
				time += dt;
			}

			void UIManager::Render() {
				if (rootElement->visible)
					rootElement->Render();

				Cursor* c = GetCurrentCursor();
				if (c)
					c->Render(mouseCursorPosition);
			}

			void UIManager::Copy(const std::string& text) { SDL_SetClipboardText(text.c_str()); }

			std::string UIManager::Paste() {
				char* txt = SDL_GetClipboardText();
				if (!txt)
					return std::string();
				std::string data = txt;
				SDL_free(txt);
				return data;
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
