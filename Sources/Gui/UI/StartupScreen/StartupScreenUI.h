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

#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class IRenderer;
		class IAudioDevice;
		class FontManager;
	}
	namespace gui {
		class StartupScreenHelper;
		class StartupScreenMainMenu;
		namespace ui {
			class UIManager;
		}

		/** The startup (setup) window UI: title bar, logo and the settings menu. */
		class StartupScreenUI : public RefCountedObject {
			Handle<client::IRenderer> renderer;
			Handle<client::IAudioDevice> audioDevice;
			Handle<client::FontManager> fontManager;
			Handle<StartupScreenHelper> helper;

			Handle<ui::UIManager> manager;
			Handle<StartupScreenMainMenu> mainMenu;

			void Init();

		protected:
			~StartupScreenUI();

		public:
			bool shouldExit = false;

			StartupScreenUI(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
			                client::FontManager* fontManager, StartupScreenHelper* helper);

			client::FontManager& GetFontManager() { return *fontManager; }
			StartupScreenHelper& GetHelper() { return *helper; }
			ui::UIManager& GetUIManager() { return *manager; }

			/** Reloads the whole UI while preserving state (e.g. language change). */
			void Reload();

			void SetupRenderer();

			void MouseEvent(float x, float y);
			void WheelEvent(float x, float y);
			void KeyEvent(const std::string& key, bool down);
			void TextInputEvent(const std::string& text);
			void TextEditingEvent(const std::string& text, int start, int len);
			bool AcceptsTextInput();
			AABB2 GetTextInputRect();

			void RunFrame(float dt);
			void RunFrameLate(float dt);

			void Closing();
			bool WantsToBeClosed();
		};
	} // namespace gui
} // namespace spades
