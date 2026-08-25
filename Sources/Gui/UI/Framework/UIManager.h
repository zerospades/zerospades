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
#include <vector>

#include <Core/Math.h>
#include <Core/RefCountedObject.h>

#include "Cursor.h"
#include "Event.h"
#include "Timer.h"
#include "UIElement.h"

namespace spades {
	namespace client {
		class IRenderer;
		class IAudioDevice;
	}
	namespace gui {
		namespace ui {
			/** Manages all input/output and rendering of the UI framework. */
			class UIManager : public RefCountedObject {
				Handle<client::IRenderer> renderer;
				Handle<client::IAudioDevice> audioDevice;

				Handle<UIElement> rootElement;
				Handle<Cursor> defaultCursor;

				// Focus/capture/hover are held as strong references so the target
				// stays alive even after it is detached from the tree; they are
				// cleared eagerly by `DiscardElement` when a subtree leaves.
				Handle<UIElement> activeElement;
				Handle<UIElement> mouseCapturedElement;
				Handle<UIElement> mouseHoverElement;

				std::vector<Handle<Timer>> timers;
				KeyRepeatManager keyRepeater;
				KeyRepeatManager charRepeater;

				PasteClipboardEventHandler clipboardEventHandler;

				MouseButton TranslateMouseButton(const std::string& key);
				UIElement* GetMouseActiveElement();
				Cursor* GetCurrentCursor();
				void MouseEventDone();
				void HandleKeyInner(const std::string& key);
				void HandleCharInner(const std::string& chr);

			protected:
				~UIManager();

			public:
				Vector2 mouseCursorPosition;
				float time = 0.0F;
				bool isControlPressed = false;
				bool isShiftPressed = false;
				bool isAltPressed = false;
				bool isMetaPressed = false;

				// IME (Input Method Editor) support
				std::string editingText;
				int editingStart = 0;
				int editingLength = 0;

				float screenWidth = 0.0F;
				float screenHeight = 0.0F;

				UIManager(client::IRenderer* renderer, client::IAudioDevice* audioDevice);

				client::IRenderer& GetRenderer() const { return *renderer; }
				client::IAudioDevice& GetAudioDevice() const { return *audioDevice; }

				UIElement& GetRootElement() const { return *rootElement; }
				Cursor* GetDefaultCursor() const { return defaultCursor.GetPointerOrNull(); }

				UIElement* GetActiveElement() const { return activeElement.GetPointerOrNull(); }
				void SetActiveElement(UIElement* element) { activeElement = element; }

				/** Drop any weak reference held into the subtree rooted at `element`. */
				void DiscardElement(UIElement* element);

				void WheelEvent(float x, float y);
				void MouseEvent(float x, float y);

				/** Forces key repeat to stop and clears transient input state. */
				void KeyPanic();
				void KeyEvent(const std::string& key, bool down);
				void TextInputEvent(const std::string& chr);
				void TextEditingEvent(const std::string& chr, int start, int len);
				bool AcceptsTextInput();
				AABB2 GetTextInputRect();

				void ProcessHotKey(const std::string& key);

				void AddTimer(Timer* timer);
				void RemoveTimer(Timer* timer);

				void PlaySound(const std::string& filename);

				void RunFrame(float dt);
				void Render();

				void Copy(const std::string& text);
				void Paste(PasteClipboardEventHandler handler);
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
