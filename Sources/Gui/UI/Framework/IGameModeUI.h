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

#pragma once

#include <string>
#include <Core/Math.h>

namespace spades {
	namespace client {
		class IRenderer;
		class IAudioDevice;
		class FontManager;
	} // namespace client
	namespace gui {
		namespace ui {
			class UIManager;
		} // namespace ui

		/**
		 * Shared UI management interface for game modes (Client gameplay, KV6 Editor).
		 * Implementations manage UI elements, input routing, and lifecycle for their respective modes.
		 * Both ClientUI and EditorUI implement this contract.
		 */
		class IGameModeUI : public RefCountedObject {
		public:
			virtual ~IGameModeUI() = default;

			// Input handling
			virtual void MouseEvent(float x, float y) = 0;
			virtual void WheelEvent(float x, float y) = 0;
			virtual void KeyEvent(const std::string& key, bool down) = 0;
			virtual void TextInputEvent(const std::string& text) = 0;
			virtual bool AcceptsTextInput() = 0;
			virtual AABB2 GetTextInputRect() = 0;

			// Frame & lifecycle
			virtual void RunFrame(float dt) = 0;
			virtual void Closing() = 0;
			virtual bool WantsToClose() = 0;

			// Resource access
			virtual client::IRenderer* GetRenderer() = 0;
			virtual client::IAudioDevice* GetAudioDevice() = 0;
			virtual client::FontManager& GetFontManager() = 0;
			virtual ui::UIManager& GetUIManager() = 0;

			// State queries
			virtual bool NeedsAbsoluteMouseCoordinate() = 0;
		};
	} // namespace gui
} // namespace spades
