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

namespace spades {
	namespace client {
		class IRenderer;
		class FontManager;
		class IAudioDevice;
	} // namespace client
	namespace gui {
		/**
		 * Secondary toolbar showing the active tool's sub-tools and options
		 * (toggles, colour swatch, labels). Sits below the main toolbar.
		 */
		class OptionBar {
		public:
			enum class OptionType { Bool, Color, Label };

			struct Option {
				std::string group;
				std::string label;
				OptionType type = OptionType::Label;
				bool bvalue = false;
				uint32_t color = 0xC8C8C8;
			};

			struct SubToolButton {
				std::string label;
				bool active = false;
			};

			OptionBar(client::IAudioDevice* audioDevice = nullptr);

			void SetSubToolButtons(const std::vector<SubToolButton>& buttons);
			void SetOptions(const std::vector<Option>& options);

			float OptionX(int index, float& outWidth) const;
			float HitTest(const Vector2& p); // returns option index or -1.0
			bool IsSubToolButtonHit(const Vector2& p, int& outIndex) const;
			bool IsOptionHovered(const Vector2& p, int index) const;

			void Draw(client::IRenderer& renderer, client::FontManager& fontManager,
			         const Vector2& cursorPos, bool menuActive, float screenWidth);
			void PlayButtonActivateSound() { PlayClickSound(); }

			// Callbacks (optional)
			std::function<void(int)> OnSubToolClicked;
			std::function<void(int)> OnBoolToggled;
			std::function<void(int)> OnColorClicked;

		private:
			client::IAudioDevice* audioDevice = nullptr;
			std::vector<SubToolButton> subToolButtons;
			std::vector<Option> options;
			std::vector<bool> previousSubToolHoverState;
			std::vector<bool> previousOptionHoverState;

			void PlayHoverSound() const;
			void PlayClickSound() const;
			bool InRect(const Vector2& p, float x, float y, float w, float h) const;
		};
	} // namespace gui
} // namespace spades
