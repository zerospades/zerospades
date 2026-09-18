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
		 *
		 * Buttons are as wide as their text, so the layout is made while
		 * drawing, where the font is at hand, and clicks are tested against the
		 * frame last drawn: always what the user sees.
		 */
		class OptionBar {
		public:
			enum class OptionType { Bool, Color, Label, Action };

			struct Option {
				std::string group; // consecutive options of one group sit together
				std::string label;
				OptionType type = OptionType::Label;
				bool bvalue = false;
				bool enabled = true; // false greys a toggle or action out and ignores clicks
				uint32_t color = 0xC8C8C8;
			};

			struct SubToolButton {
				std::string label;
				std::string hotKey; // drawn on the button's right; empty for none
				bool active = false;
			};

			OptionBar(client::IAudioDevice* audioDevice = nullptr);

			void SetSubToolButtons(const std::vector<SubToolButton>& buttons);
			void SetOptions(const std::vector<Option>& options);

			/**
			 * Runs the callback of the enabled sub-tool button or option under
			 * `p`; false if there is none.
			 */
			bool Click(const Vector2& p);
			void Draw(client::IRenderer& renderer, client::FontManager& fontManager,
			         const Vector2& cursorPos, bool menuActive, float screenWidth);

			// Called with the clicked sub-tool's or option's index.
			std::function<void(int)> OnSubToolClicked;
			std::function<void(int)> OnBoolToggled;
			std::function<void(int)> OnColorClicked;
			std::function<void(int)> OnActionClicked;

		private:
			// Horizontal extent of something drawn on the bar.
			struct Span {
				float x, width;
			};

			client::IAudioDevice* audioDevice = nullptr;
			std::vector<SubToolButton> subToolButtons;
			std::vector<Option> options;
			// Where each sub-tool button and option was last drawn.
			std::vector<Span> subToolSpans;
			std::vector<Span> optionSpans;
			std::vector<bool> previousSubToolHoverState;
			std::vector<bool> previousOptionHoverState;

			void PlayHoverSound() const;
			void PlayClickSound() const;
		};
	} // namespace gui
} // namespace spades
