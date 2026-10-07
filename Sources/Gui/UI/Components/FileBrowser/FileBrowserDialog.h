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

#include "FileBrowserView.h"
#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		/**
		 * A `FileBrowserView` in a modal, centered frame: the standard way to ask
		 * for a path from anywhere (open, save, import, pick a folder).
		 *
		 * It disables its owner while shown and reports once through `closed`,
		 * whether it was confirmed or cancelled.
		 */
		class FileBrowserDialog : public ui::UIElement {
		public:
			FileBrowserDialog(ui::UIElement* owner, const std::string& title,
			                  FileBrowserOptions options);

			/** Called once, with `accepted` false if the dialog was cancelled. */
			std::function<void(const FileBrowserResult&)> closed;

			FileBrowserView& GetBrowser() { return *browser; }

			void Run();
			void Close(const FileBrowserResult& result);

		private:
			ui::UIElement* owner;      // weak
			FileBrowserView* browser;  // weak; owned as a child
			bool closing = false;       // guards a second Close (e.g. Esc while saving)
			bool disabledOwner = false; // whether Run() disabled the owner
		};
	} // namespace gui
} // namespace spades
