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

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Framework/FeedbackSounds.h>

namespace spades {
	namespace client {
		class IRenderer;
		class FontManager;
		class IAudioDevice;
	} // namespace client
	namespace gui {
		/**
		 * Unified top toolbar: modes on the left, a separator, then the tools
		 * available in the current mode, optionally a colour swatch (the colour
		 * the tools draw with, whichever is active), then undo/redo buttons on
		 * the right.
		 *
		 * The host describes every button, so the toolbar holds no editor
		 * policy: a disabled button draws greyed out and ignores clicks. Tool
		 * buttons sharing a `group` sit together, and a separator divides one
		 * group from the next.
		 *
		 * A click reports the id of the button drawn under it, never a position:
		 * if the host changed its buttons since that frame was drawn, it looks
		 * the id up in what it has now and ignores one it no longer has.
		 */
		class Toolbar {
		public:
			struct ToolbarButton {
				std::string id; // reported when clicked
				std::string label;
				std::string hotKey; // drawn on the button's right; empty for none
				bool enabled = true;
				bool active = false;
				int group = 0; // tool buttons only
			};

			Toolbar(client::IAudioDevice* audioDevice = nullptr);

			void SetModeButtons(const std::vector<ToolbarButton>& buttons);
			void SetToolButtons(const std::vector<ToolbarButton>& buttons);
			void SetUndoButton(bool enabled);
			void SetRedoButton(bool enabled);
			/**
			 * Shows a swatch of `color` (0x00BBGGRR) after the tools, marked
			 * while `open` (its picker is showing); clicking it calls
			 * OnColorSwatchClicked. Hidden until set.
			 */
			void SetColorSwatch(uint32_t color, bool open);

			/** Runs the callback of the enabled button under `p`; false if there is none. */
			bool Click(const Vector2& p, float screenWidth);
			void Draw(client::IRenderer& renderer, client::FontManager& fontManager,
			         const Vector2& cursorPos, bool menuActive, float screenWidth);

			// Called with the clicked button's id.
			std::function<void(const std::string& id)> OnModeClicked;
			std::function<void(const std::string& id)> OnToolClicked;
			std::function<void()> OnUndoClicked;
			std::function<void()> OnRedoClicked;
			std::function<void()> OnColorSwatchClicked;

		private:
			enum class Kind { Mode, Tool, ColorSwatch, Undo, Redo };

			// One button placed on the bar; `index` is into its kind's list.
			struct Slot {
				Kind kind;
				int index;
				float x, width;
				bool separatorBefore;
			};

			ui::FeedbackSounds sounds;
			std::vector<ToolbarButton> modeButtons;
			std::vector<ToolbarButton> toolButtons;
			bool undoEnabled = false;
			bool redoEnabled = false;
			bool hasSwatch = false;
			uint32_t swatchColor = 0;
			bool swatchOpen = false;
			std::vector<bool> previousHoverState; // per slot, for the hover sound

			// Every button in drawing order, shared by Draw and Click so a click
			// always lands on the button drawn under it.
			std::vector<Slot> Layout(float screenWidth) const;
			ToolbarButton ButtonOf(const Slot& slot) const;
			void DrawColorSwatch(client::IRenderer& renderer, const Slot& slot, bool hover) const;
		};
	} // namespace gui
} // namespace spades
