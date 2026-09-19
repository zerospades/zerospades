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

#include <memory>

#include <Client/Fonts.h>
#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Core/Math.h>
#include <Gui/UI/Framework/IGameModeUI.h>

namespace spades {
	namespace gui {
		class MapEditorView;
		class EditorMenu;
		class SoftwareCursor;

		namespace ui {
			class UIManager;
		} // namespace ui

		/**
		 * UI management for the map editor.
		 * Owns and coordinates the menu and any other map editor UI components.
		 * Centralizes input handling and rendering following the same pattern as EditorUI (KV6Editor).
		 */
		class MapEditorUI : public IGameModeUI {
			Handle<client::IRenderer> renderer;
			Handle<client::IAudioDevice> audioDevice;
			Handle<client::FontManager> fontManager;
			Handle<ui::UIManager> uiManager;

			std::unique_ptr<EditorMenu> editorMenu;
			std::unique_ptr<SoftwareCursor> ownedCursor; // if no cursor was provided
			SoftwareCursor* cursor = nullptr;

			// weak reference to the editor view
			MapEditorView* editor;

			bool shouldClose = false;

		protected:
			~MapEditorUI();

		public:
			MapEditorUI(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
			            client::FontManager* fontManager, MapEditorView* editor,
			            SoftwareCursor* cursor);
			void EditorDestroyed();

			// IGameModeUI implementation
			client::IRenderer* GetRenderer() override { return renderer.GetPointerOrNull(); }
			client::IAudioDevice* GetAudioDevice() override { return audioDevice.GetPointerOrNull(); }
			client::FontManager& GetFontManager() override { return *fontManager; }
			ui::UIManager& GetUIManager() override { return *uiManager; }

			void MouseEvent(float x, float y) override;
			void WheelEvent(float x, float y) override;
			void KeyEvent(const std::string& key, bool down) override;
			void TextInputEvent(const std::string& text) override;
			bool AcceptsTextInput() override;
			AABB2 GetTextInputRect() override;
			bool NeedsAbsoluteMouseCoordinate() override { return false; }

			void RunFrame(float dt) override;
			void Closing() override;
			bool WantsToClose() override { return shouldClose; }

			// Component accessors for MapEditorView
			EditorMenu* GetEditorMenu() { return editorMenu.get(); }
			SoftwareCursor* GetCursor() { return cursor; }
		};
	} // namespace gui
} // namespace spades
