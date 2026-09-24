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
#include <vector>

namespace spades {
	namespace gui {
		/**
		 * The voxel model file types this program knows: which it lists, which it
		 * can open as a document, and what a new document is called. Everything
		 * with an opinion about a model's type asks here, so teaching the program a
		 * format is one edit in one place.
		 */

		/** Every model file type the editor lists, whether or not it can open one. */
		const std::vector<std::string>& KV6ModelExtensions();

		/** The extension a document is saved with when its name names no type. */
		const std::string& KV6DocumentExtension();

		/** Whether a file of this name can be opened as a document, as opposed to
		 *  merely being listed. */
		bool KV6IsEditable(const std::string& name);

		/** `name` with the document extension appended, unless it already names a
		 *  type the editor lists. A name that is nothing but an extension would
		 *  become a hidden, nameless file, so it keeps the extension it was given
		 *  and the caller's own validation rejects it. */
		std::string KV6DocumentFileName(const std::string& name);

		/** The name a document that has never been saved is offered under. */
		std::string KV6UntitledFileName();

		/** The label the file browser shows for the model filter. */
		std::string KV6ModelFilterLabel();

		/** Why a listed file cannot be opened, shown beside its greyed-out row. */
		std::string KV6UnsupportedHint();

		/** The same, said in full to somebody who tried to open one anyway. */
		std::string KV6UnsupportedMessage();
	} // namespace gui
} // namespace spades
