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
#include <vector>

#include <Core/Math.h>

namespace spades {
	namespace client {
		class IRenderer;
		class FontManager;
		class IAudioDevice;
	} // namespace client
	namespace gui {
		/**
		 * Unified top toolbar: modes on the left, a separator, then the tools
		 * available in the current mode, then undo/redo buttons on the right.
		 */
		class Toolbar {
		public:
			enum class ClickType { None, Mode, Tool, Undo, Redo };

			struct ClickResult {
				ClickType type = ClickType::None;
				int index = -1;
			};

			struct ToolbarButton {
				std::string label;
				bool enabled = true;
				bool active = false;
			};

			Toolbar(client::IAudioDevice* audioDevice = nullptr);

			void SetModeButtons(const std::vector<std::string>& labels);
			void SetActiveModeButton(int index);
			void SetToolButtons(const std::vector<ToolbarButton>& buttons);
			void SetUndoButton(bool enabled);
			void SetRedoButton(bool enabled);

			ClickResult HitTest(const Vector2& p, float screenWidth);
			void Draw(client::IRenderer& renderer, client::FontManager& fontManager,
			         const Vector2& cursorPos, bool menuActive, float screenWidth);
			void PlayButtonActivateSound() { PlayClickSound(); }

			// Callbacks (optional; called instead of modifying state)
			std::function<void(int)> OnModeClicked;
			std::function<void(int)> OnToolClicked;
			std::function<void()> OnUndoClicked;
			std::function<void()> OnRedoClicked;

		private:
			client::IAudioDevice* audioDevice = nullptr;
			std::vector<std::string> modeButtons;
			std::vector<ToolbarButton> toolButtons;
			std::vector<bool> previousHoverState; // track hover transitions for sound
			bool undoEnabled = false;
			bool redoEnabled = false;
			bool previousUndoHover = false;
			bool previousRedoHover = false;
			int activeModeButton = 0;

			void PlayHoverSound() const;
			void PlayClickSound() const;
			float ToolbarX(int slot) const;
			float UndoButtonX(float sw, bool redo) const;
			bool InRect(const Vector2& p, float x, float y, float w, float h) const;
		};
	} // namespace gui
} // namespace spades
