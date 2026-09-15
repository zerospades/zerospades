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
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		class KV6EditorView;
		class ColorPicker;
		class EditorMenu;
		class OptionBar;
		class SoftwareCursor;
		class Toolbar;

		/**
		 * UI management for the KV6 editor.
		 * Owns toolbar, options panel, color picker, and menu.
		 * Delegates most operations back to KV6EditorView as needed.
		 */
		class EditorUI : public IGameModeUI {
			Handle<client::IRenderer> renderer;
			Handle<client::IAudioDevice> audioDevice;
			Handle<client::FontManager> fontManager;
			Handle<ui::UIManager> uiManager;

			std::unique_ptr<Toolbar> toolbar;
			std::unique_ptr<OptionBar> optionBar;
			std::unique_ptr<ColorPicker> colorPicker;
			std::unique_ptr<EditorMenu> editorMenu;

			// weak reference to the editor context
			KV6EditorView* editor;

			bool shouldClose = false;

		protected:
			~EditorUI();

		public:
			EditorUI(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
			         client::FontManager* fontManager, KV6EditorView* editor,
			         SoftwareCursor* cursor);
			void EditorDestroyed();

			// IGameModeUI implementation
			client::IRenderer* GetRenderer() override { return &*renderer; }
			client::IAudioDevice* GetAudioDevice() override { return &*audioDevice; }
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

			// Component accessors for KV6EditorView
			Toolbar* GetToolbar() { return toolbar.get(); }
			OptionBar* GetOptionBar() { return optionBar.get(); }
			ColorPicker* GetColorPicker() { return colorPicker.get(); }
			EditorMenu* GetEditorMenu() { return editorMenu.get(); }
		};
	} // namespace gui
} // namespace spades
