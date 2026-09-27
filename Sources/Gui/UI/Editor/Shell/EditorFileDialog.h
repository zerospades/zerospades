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

#include <string>

#include <Gui/UI/Components/FileBrowser/FileBrowserView.h>

namespace spades {
	namespace gui {
		/**
		 * The browser options every model file dialog starts from: the main screen's
		 * model tab and the editor's Open, Save As and Insert dialogs. Sharing them
		 * is what keeps a model file behaving the same way in all of them — the same
		 * types listed, a type that cannot be opened greyed out and saying why, and
		 * folders that can be made, renamed and deleted.
		 */
		FileBrowserOptions KV6ModelBrowserOptions();

		/** The folder a model dialog opens in, remembered across dialogs and across
		 *  runs. Falls back to `fallbackDir` when nothing has been remembered yet. */
		std::string KV6RememberedFolder(const std::string& fallbackDir);

		/** Remembers `directory` as where the next model dialog opens. Every browser
		 *  reports its folder here, so the editor and the main screen follow each
		 *  other instead of each keeping their own idea of where the player is. */
		void KV6RememberFolder(const std::string& directory);
	} // namespace gui
} // namespace spades
