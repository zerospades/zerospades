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
		/** The editor a document opens in. */
		enum class DocumentKind {
			Model, // one voxel model (.kv6)
			Scene, // a scene of named voxel models (.2kv6)
			Map    // a voxel terrain map (.vxl)
		};

		/**
		 * A file type the editors know: what it is called, which editor opens
		 * it, and whether one can yet. A type no editor opens is still listed,
		 * so a file the editors will eventually read is visible where the others
		 * are rather than leaving the player wondering where it went.
		 */
		struct DocumentType {
			const char* extension;   // with the dot: ".kv6"
			const char* description; // what the desktop calls it: "KV6 voxel model"
			DocumentKind kind;
			bool editable;
		};

		/**
		 * Every document type, in the order the program prefers them. Everything
		 * with an opinion about a document's type asks here, so teaching the
		 * program a format is one edit in one place.
		 */
		const std::vector<DocumentType>& DocumentTypes();

		/** The type a file of this name has, or null when it has none of them. */
		const DocumentType* FindDocumentType(const std::string& name);

		/** Whether a file of this name can be opened in an editor, as opposed to
		 *  merely being listed. */
		bool IsEditableDocument(const std::string& name);

		/** The extension of every listed type, whether or not it can be opened. */
		const std::vector<std::string>& DocumentExtensions();

		/** The extension a `kind` document is saved with when its name names no
		 *  type: the first editable type of that kind; empty when there is none. */
		const std::string& DefaultExtension(DocumentKind kind);

		/** `name` with `kind`'s default extension appended, unless it already
		 *  names a listed type. A name that is nothing but an extension would
		 *  become a hidden, nameless file, so it keeps the extension it was given
		 *  and the caller's own validation rejects it. */
		std::string DocumentFileName(const std::string& name, DocumentKind kind);

		/** The name a `kind` document that has never been saved is offered under. */
		std::string UntitledFileName(DocumentKind kind);

		/** The label the file browser shows for the filter listing every type. */
		std::string DocumentFilterLabel();

		/** Why a listed file cannot be opened, shown beside its greyed-out row. */
		std::string UnsupportedDocumentHint();

		/** The same, said in full to somebody who tried to open one anyway. */
		std::string UnsupportedDocumentMessage();
	} // namespace gui
} // namespace spades
