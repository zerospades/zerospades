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
#include <vector>

#include <Gui/View.h>
#include <Gui/UI/Components/EditorMenu.h>
#include <Client/Client.h>
#include <Client/EditorNetClient.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class IRenderer;
		class IAudioDevice;
		class FontManager;
	}
	namespace gui {
		class SoftwareCursor;
		class MapEditorUI;

		/**
		 * Map editor view for .vxl files.
		 *
		 * Loads a voxel terrain map and provides a local game client for viewing
		 * and editing using the game engine. Uses MapEditorUI to manage UI components.
		 */
		class MapEditorView : public View, public IEditorMenuHost {
		public:
			MapEditorView(client::IRenderer* r, client::IAudioDevice* dev,
			              client::FontManager* fm, SoftwareCursor* cursor,
			              const std::string& path);

			void MouseEvent(float x, float y) override;
			void WheelEvent(float x, float y) override;
			void KeyEvent(const std::string&, bool down) override;
			void TextInputEvent(const std::string&) override;
			bool AcceptsTextInput() override;
			AABB2 GetTextInputRect() override;
			bool NeedsAbsoluteMouseCoordinate() override;
			bool OnMenuEscape() override { return false; }

			void RunFrame(float dt) override;
			void RunFrameLate(float dt) override;
			void Closing() override;
			bool WantsToBeClosed() override { return wantsClose; }

			// IEditorMenuHost implementation
			std::string GetMenuTitle() override { return "Map Editor"; }
			std::vector<EditorMenuItem> GetMenuItems() override;

			/** Writes the map to `path`; the file it was opened from by default. */
			void SaveDocument(const std::string& path);

		protected:
			~MapEditorView();

		private:
			Handle<client::IRenderer> renderer;
			Handle<client::IAudioDevice> audioDevice;
			Handle<client::FontManager> fontManager;
			Handle<client::Client> client;
			Handle<MapEditorUI> ui;
			std::string filePath;
			bool wantsClose = false;
			bool clientCreated = false;
		};
	} // namespace gui
} // namespace spades
