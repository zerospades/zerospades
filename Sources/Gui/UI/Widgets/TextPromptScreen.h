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

#include <functional>
#include <string>

#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		namespace ui {
			class Field;
			class Label;
		} // namespace ui

		/**
		 * A modal single-line text prompt: title, editable field, confirm and
		 * Cancel buttons. While shown it disables its owner.
		 *
		 * An optional validator runs on confirm; a non-empty message is shown under
		 * the field and keeps the prompt open, so callers never have to re-open it
		 * to report a bad value.
		 */
		class TextPromptScreen : public ui::UIElement {
		public:
			/** Returns an error message for `text`, or an empty string to accept it. */
			using Validator = std::function<std::string(const std::string& text)>;

			struct Options {
				std::string title;
				std::string initialText;
				std::string confirmCaption; // empty: "OK"
				std::string placeholder;
				Validator validate;
				/**
				 * Select only the part before the last '.', so typing replaces a
				 * file name but keeps its extension ("model.kv6" -> "model").
				 */
				bool selectStem = false;
			};

			ui::EventHandler closed;

			TextPromptScreen(ui::UIElement* owner, Options options);

			/** True if the prompt was confirmed (and the validator accepted it). */
			bool GetResult() const { return result; }
			/** The confirmed text; meaningful only if `GetResult()`. */
			const std::string& GetText() const { return text; }

			void Run();
			void Close();
			void HotKey(const std::string& key) override;

		private:
			ui::UIElement* owner;            // weak
			ui::Field* field = nullptr;      // weak; owned as a child
			ui::Label* errorLabel = nullptr; // weak; owned as a child
			Validator validate;

			bool result = false;
			std::string text;

			void OnConfirm();
			void OnCancel();
		};
	} // namespace gui
} // namespace spades
