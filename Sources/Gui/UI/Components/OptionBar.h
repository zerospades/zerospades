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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Framework/FeedbackSounds.h>
#include <Gui/UI/Widgets/NumberEntry.h>

namespace spades {
	namespace client {
		class IRenderer;
		class IFont;
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
			enum class OptionType { Bool, Label, Action, Number };

			struct Option {
				std::string id;    // reported when clicked
				std::string group; // consecutive options of one group sit together
				std::string label; // a button's caption, or a Number's axis letter
				OptionType type = OptionType::Label;
				bool bvalue = false;
				bool enabled = true; // false greys a toggle or action out and ignores clicks
				float value = 0.0F;  // Number: what the box shows when it is not being typed into
				float step = 1.0F;   // Number: what one press of its arrows adds
				int decimals = 1;    // Number: decimal places it is written with
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
			 * `p`; false if there is none. A click that lands anywhere else
			 * settles a number being typed, so the caller reports every click,
			 * including the ones over the viewport.
			 */
			bool Click(const Vector2& p);
			void Draw(client::IRenderer& renderer, client::FontManager& fontManager,
			         const Vector2& cursorPos, bool menuActive, float screenWidth);

			// --- Typing into a Number option ---------------------------------
			//
			// The bar has no retained widgets to take keyboard focus, so the host
			// asks it whether it is being typed into and hands it what it needs
			// (see NumberEntry). While it is, the host must not read those keys
			// itself: a tool hot key would fire on every letter typed.

			/** True while a number is being typed into. */
			bool IsEditing() const { return editing; }
			/** Takes a key while editing; false if it means nothing here. */
			bool KeyEvent(const std::string& key);
			void TextInputEvent(const std::string& text);
			/** The box being typed into, for the IME; empty when none is. */
			AABB2 EditingRect() const;
			/**
			 * Stops editing. `commit` reports the typed value one last time as
			 * settled; otherwise the box goes back to what it was showing.
			 */
			void EndEditing(bool commit);

			/** The host's current owner, asked on each click; unset, every click counts. */
			std::function<const void*()> CurrentOwner;

			// Called with the clicked sub-tool's index, or the clicked option's id.
			std::function<void(int index)> OnSubToolClicked;
			std::function<void(const std::string& id)> OnBoolToggled;
			std::function<void(const std::string& id)> OnActionClicked;
			/**
			 * A Number changed. `committed` is false while it is still being
			 * typed and true once the user has settled on it, so a host can show
			 * a value live and record only the one that was meant.
			 */
			std::function<void(const std::string& id, float value, bool committed)> OnNumberChanged;

		private:
			// Horizontal extent of something drawn on the bar.
			struct Span {
				float x, width;
			};

			// Where a Number option's parts went, so a click can tell the box
			// from the arrows. Both arrows share one column, splitting the bar's
			// button height between them.
			struct NumberSpans {
				Span box{0.0F, 0.0F};
				Span arrows{0.0F, 0.0F};
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
			// Parallel to `optionSpans`; only the Number entries are meaningful.
			std::vector<NumberSpans> numberSpans;

			// The number being typed into, held across the rebuild the host does
			// every frame: it is keyed by owner and id rather than by position,
			// so an option list that changes shape under the caret never moves
			// the edit onto a different box.
			bool editing = false;
			const void* editingOwner = nullptr;
			std::string editingId;
			// What the box showed when typing started: what it goes back to when
			// the edit is abandoned or the text never became a number.
			float editingStartValue = 0.0F;
			NumberEntry entry;

			// Reports `value` for `id` through OnNumberChanged.
			void ReportNumber(const std::string& id, float value, bool committed);
			// Starts typing into option `i`, settling whatever was being typed.
			void BeginEditing(size_t i);
			// Steps option `i` by `delta` and reports it as settled.
			void StepNumber(size_t i, float delta);
			/** The option being typed into, or -1 when none is (or it has gone). */
			int EditingIndex() const;
			// Paints option `i`'s axis letter, box and arrows, and records where
			// they went for the next click.
			void DrawNumber(client::IRenderer& renderer, client::IFont& font, const Option& op,
			                std::size_t i, const Span& span, const Vector2& labelSize,
			                bool isEditing, bool menuActive, const Vector2& cursorPos);
		};
	} // namespace gui
} // namespace spades
