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

#include "EditorUI.h"
#include "KV6EditorView.h"
#include <Client/Fonts.h>
#include <Gui/UI/Components/ColorPicker.h>
#include <Gui/UI/Components/EditorMenu.h>
#include <Gui/UI/Components/OptionBar.h>
#include <Gui/UI/Components/SoftwareCursor.h>
#include <Gui/UI/Components/Toolbar.h>
#include <Core/Debug.h>

namespace spades {
	namespace gui {
		EditorUI::EditorUI(client::IRenderer* _renderer, client::IAudioDevice* _audioDevice,
		                   client::FontManager* _fontManager, KV6EditorView* _editor,
		                   SoftwareCursor* cursor)
		    : renderer(_renderer), audioDevice(_audioDevice), fontManager(_fontManager),
		      uiManager(new ui::UIManager(_renderer, _audioDevice)),
		      editor(_editor) {
			SPADES_MARK_FUNCTION();
			try {
				toolbar = std::make_unique<Toolbar>(_audioDevice);
				optionBar = std::make_unique<OptionBar>(_audioDevice);
				colorPicker = std::make_unique<ColorPicker>();
				editorMenu = std::make_unique<EditorMenu>(*_editor, *_renderer, *_fontManager, *cursor, _audioDevice);
			} catch (const std::exception& ex) {
				SPLog("[!] Failed to initialize editor UI: %s", ex.what());
				throw;
			}
		}

		EditorUI::~EditorUI() {
			EditorDestroyed();
		}

		void EditorUI::EditorDestroyed() {
			SPADES_MARK_FUNCTION();
			editor = nullptr;
		}

		void EditorUI::MouseEvent(float x, float y) {
			// Mouse events are not handled by EditorUI; delegated to KV6EditorView
		}

		void EditorUI::WheelEvent(float x, float y) {
			// Wheel events are not handled by EditorUI; delegated to KV6EditorView
		}

		void EditorUI::KeyEvent(const std::string& key, bool down) {
			// Key events are not handled by EditorUI; delegated to KV6EditorView
		}

		void EditorUI::TextInputEvent(const std::string& text) {
			// Text input events are not handled by EditorUI; delegated to KV6EditorView
		}

		bool EditorUI::AcceptsTextInput() {
			return false;
		}

		AABB2 EditorUI::GetTextInputRect() {
			return AABB2();
		}

		void EditorUI::RunFrame(float dt) {
			// UI components don't have per-frame updates needed yet
		}

		void EditorUI::Closing() {
			SPADES_MARK_FUNCTION();
			shouldClose = true;
		}
	} // namespace gui
} // namespace spades
