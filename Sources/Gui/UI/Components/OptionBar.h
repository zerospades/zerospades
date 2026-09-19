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
		 * Secondary toolbar showing the active tool's sub-tools and options
		 * (toggles, actions, labels). Sits below the main toolbar.
		 *
		 * Buttons are as wide as their text, so the layout is made while
		 * drawing, where the font is at hand, and clicks are tested against the
		 * frame last drawn: always what the user sees.
		 *
		 * The content belongs to an owner (whose options they are: the active
		 * tool). A click only counts while that owner is still the host's
		 * current one, so a click landing just after the host switched owners
		 * never acts on the new owner through the old one's buttons. Options
		 * are reported by id, sub-tool buttons by position within their owner.
		 */
		class OptionBar {
		public:
			enum class OptionType { Bool, Label, Action };

			struct Option {
				std::string id;    // reported when clicked
				std::string group; // consecutive options of one group sit together
				std::string label;
				OptionType type = OptionType::Label;
				bool bvalue = false;
				bool enabled = true; // false greys a toggle or action out and ignores clicks
			};

			struct SubToolButton {
				std::string label;
				std::string hotKey; // drawn on the button's right; empty for none
				bool active = false;
			};

			OptionBar(client::IAudioDevice* audioDevice = nullptr);

			/** Fills the bar with `owner`'s sub-tool buttons and options. */
			void SetContent(const void* owner, std::vector<SubToolButton> subToolButtons,
			                std::vector<Option> options);

			/**
			 * Runs the callback of the enabled sub-tool button or option under
			 * `p`; false if there is none.
			 */
			bool Click(const Vector2& p);
			void Draw(client::IRenderer& renderer, client::FontManager& fontManager,
			         const Vector2& cursorPos, bool menuActive, float screenWidth);

			/** The host's current owner, asked on each click; unset, every click counts. */
			std::function<const void*()> CurrentOwner;

			// Called with the clicked sub-tool's index, or the clicked option's id.
			std::function<void(int index)> OnSubToolClicked;
			std::function<void(const std::string& id)> OnBoolToggled;
			std::function<void(const std::string& id)> OnActionClicked;

		private:
			// Horizontal extent of something drawn on the bar.
			struct Span {
				float x, width;
			};

			ui::FeedbackSounds sounds;
			const void* owner = nullptr;
			std::vector<SubToolButton> subToolButtons;
			std::vector<Option> options;
			// What was last drawn: whose content, and where each item went.
			const void* drawnOwner = nullptr;
			std::vector<Span> subToolSpans;
			std::vector<Span> optionSpans;
			std::vector<bool> previousSubToolHoverState;
			std::vector<bool> previousOptionHoverState;
		};
	} // namespace gui
} // namespace spades
