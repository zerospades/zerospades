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
#include <Client/Fonts.h>
#include <Gui/UI/Components/ColorPicker.h>
#include <Gui/UI/Components/MenuBar.h>
#include <Gui/UI/Components/OptionBar.h>
#include <Gui/UI/Components/SoftwareCursor.h>
#include <Gui/UI/Components/Toolbar.h>
#include <Gui/UI/Components/UIOverlayHost.h>
#include <Core/Debug.h>

namespace spades {
	namespace gui {
		EditorUI::EditorUI(client::IRenderer* _renderer, client::IAudioDevice* _audioDevice,
		                   client::FontManager* _fontManager, IMenuBarHost& menuHost,
		                   SoftwareCursor* cursor) {
			SPADES_MARK_FUNCTION();
			try {
				overlay = std::make_unique<UIOverlayHost>(*_renderer, _audioDevice, *_fontManager,
				                                          *cursor);
				toolbar = std::make_unique<Toolbar>(_audioDevice);
				optionBar = std::make_unique<OptionBar>(_audioDevice);
				contextStrip = std::make_unique<OptionBar>(_audioDevice, 1, true);
				colorPicker = std::make_unique<ColorPicker>();
				menuBar = std::make_unique<MenuBar>(menuHost, _audioDevice);
			} catch (const std::exception& ex) {
				SPLog("[!] Failed to initialize editor UI: %s", ex.what());
				throw;
			}
		}

		EditorUI::~EditorUI() {}
	} // namespace gui
} // namespace spades
