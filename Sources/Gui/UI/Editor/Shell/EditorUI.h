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
#include <Core/RefCountedObject.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		class ColorPicker;
		class MenuBar;
		class IMenuBarHost;
		class OptionBar;
		class SoftwareCursor;
		class Toolbar;
		class UIOverlayHost;

		/**
		 * The chrome every editor view shares: the menu bar, the toolbar, the
		 * option bar, the colour picker, and the overlay its modal dialogs
		 * (the file browser) are shown in. It only owns them; the view lays them
		 * out, draws them and routes input to them.
		 */
		class EditorUI : public RefCountedObject {
			std::unique_ptr<UIOverlayHost> overlay;

			std::unique_ptr<Toolbar> toolbar;
			std::unique_ptr<OptionBar> optionBar;
			// A second row of it, floating over the viewport: what an editor
			// offers only while there is something for it to work on.
			std::unique_ptr<OptionBar> contextStrip;
			std::unique_ptr<ColorPicker> colorPicker;
			std::unique_ptr<MenuBar> menuBar;

		protected:
			~EditorUI();

		public:
			/** `menuHost` supplies the menu bar's menus; it must outlive this. */
			EditorUI(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
			         client::FontManager* fontManager, IMenuBarHost& menuHost,
			         SoftwareCursor* cursor);

			// The components, for the view that lays them out and drives them
			Toolbar* GetToolbar() { return toolbar.get(); }
			OptionBar* GetOptionBar() { return optionBar.get(); }
			OptionBar* GetContextStrip() { return contextStrip.get(); }
			ColorPicker* GetColorPicker() { return colorPicker.get(); }
			MenuBar* GetMenuBar() { return menuBar.get(); }
			/** Hosts modal `spades::ui` dialogs (the file browser) over the editor. */
			UIOverlayHost* GetOverlay() { return overlay.get(); }
		};
	} // namespace gui
} // namespace spades
