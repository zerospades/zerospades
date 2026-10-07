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

namespace spades {
	namespace gui {
		class UIOverlayHost;

		/** A plain message over the editor with a single way out. */
		void ShowEditorMessage(UIOverlayHost& overlay, const std::string& text);

		/**
		 * Asks what becomes of a document's unsaved changes before it is lost:
		 * Save, Discard or Cancel. `proceed` runs after Discard, or after Save
		 * once `save` has actually written the document: `save` is handed
		 * `proceed` to call when it has (at once, or once a Save As dialog it
		 * opens has written it). Cancel runs nothing.
		 */
		void ConfirmDiscardChanges(UIOverlayHost& overlay, const std::string& question,
		                           std::function<void(std::function<void()>)> save,
		                           std::function<void()> proceed);
	} // namespace gui
} // namespace spades
