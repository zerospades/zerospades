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

#include <Client/SceneDefinition.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class IRenderer;
		class IAudioDevice;
		class FontManager;
		class GameMap;
	}
	namespace gui {
		class MainScreenHelper;
		class MainScreenMainMenu;
		namespace ui {
			class UIManager;
		}

		/** The main screen: background scene, title logo, and the main menu UI. */
		class MainScreenUI : public RefCountedObject {
			Handle<client::IRenderer> renderer;
			Handle<client::IAudioDevice> audioDevice;
			Handle<client::FontManager> fontManager;
			Handle<MainScreenHelper> helper;

			Handle<ui::UIManager> manager;
			Handle<MainScreenMainMenu> mainMenu;
			Handle<client::GameMap> titleMap;

			float time = -1.0F;

			client::SceneDefinition SetupCamera(client::SceneDefinition sceneDef, Vector3 eye,
			                                    Vector3 at, Vector3 up, float fov, float ratio);

		protected:
			~MainScreenUI();

		public:
			bool shouldExit = false;

			MainScreenUI(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
			             client::FontManager* fontManager, MainScreenHelper* helper);

			client::FontManager& GetFontManager() { return *fontManager; }
			MainScreenHelper& GetHelper() { return *helper; }
			ui::UIManager& GetUIManager() { return *manager; }

			void SetupRenderer();

			void MouseEvent(float x, float y);
			void WheelEvent(float x, float y);
			void KeyEvent(const std::string& key, bool down);
			void TextInputEvent(const std::string& text);
			void TextEditingEvent(const std::string& text, int start, int len);
			bool AcceptsTextInput();
			AABB2 GetTextInputRect();

			void RunFrame(float dt);

			void Closing();
			bool WantsToBeClosed();
		};
	} // namespace gui
} // namespace spades
