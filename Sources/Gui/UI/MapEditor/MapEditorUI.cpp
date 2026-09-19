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

#include "MapEditorUI.h"
#include "MapEditorView.h"
#include <Client/Fonts.h>
#include <Client/IRenderer.h>
#include <Gui/UI/Components/EditorMenu.h>
#include <Gui/UI/Components/SoftwareCursor.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Core/Debug.h>

namespace spades {
	namespace gui {
		MapEditorUI::MapEditorUI(client::IRenderer* _renderer, client::IAudioDevice* _audioDevice,
		                         client::FontManager* _fontManager, MapEditorView* _editor,
		                         SoftwareCursor* cursor)
		    : renderer(_renderer), audioDevice(_audioDevice), fontManager(_fontManager),
		      uiManager(new ui::UIManager(_renderer, _audioDevice)),
		      editor(_editor) {
			SPADES_MARK_FUNCTION();
			if (!_renderer)
				throw std::invalid_argument("IRenderer cannot be null");
			if (!_audioDevice)
				throw std::invalid_argument("IAudioDevice cannot be null");
			if (!_fontManager)
				throw std::invalid_argument("FontManager cannot be null");
			if (!_editor)
				throw std::invalid_argument("MapEditorView cannot be null");

			// The caller may hand over a cursor to share; otherwise this owns one,
			// as the KV6 editor does.
			if (cursor) {
				this->cursor = cursor;
			} else {
				ownedCursor = std::make_unique<SoftwareCursor>(*_renderer);
				this->cursor = ownedCursor.get();
			}

			try {
				editorMenu = std::make_unique<EditorMenu>(*_editor, *_renderer, *_fontManager,
				                                          *this->cursor, _audioDevice);
			} catch (const std::exception& ex) {
				SPLog("[!] Failed to initialize map editor UI: %s", ex.what());
				throw;
			}
		}

		MapEditorUI::~MapEditorUI() {
			EditorDestroyed();
		}

		void MapEditorUI::EditorDestroyed() {
			SPADES_MARK_FUNCTION();
			editor = nullptr;
		}

		void MapEditorUI::MouseEvent(float x, float y) {
			// Mouse events are delegated to MapEditorView
		}

		void MapEditorUI::WheelEvent(float x, float y) {
			// Wheel events are delegated to MapEditorView
		}

		void MapEditorUI::KeyEvent(const std::string& key, bool down) {
			// Key events are delegated to MapEditorView
		}

		void MapEditorUI::TextInputEvent(const std::string& text) {
			// Text input events are delegated to MapEditorView
		}

		bool MapEditorUI::AcceptsTextInput() {
			return false;
		}

		AABB2 MapEditorUI::GetTextInputRect() {
			return AABB2();
		}

		void MapEditorUI::RunFrame(float dt) {
			// UI components don't have per-frame updates needed yet
		}

		void MapEditorUI::Closing() {
			SPADES_MARK_FUNCTION();
			shouldClose = true;
		}
	} // namespace gui
} // namespace spades
