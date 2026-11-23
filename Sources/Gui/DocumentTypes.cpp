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

#include "DocumentTypes.h"

#include <cstddef>

#include <Core/LocalFileSystem.h>
#include <Core/Strings.h>

namespace spades {
	namespace gui {
		namespace fs = LocalFileSystem;

		const std::vector<DocumentType>& DocumentTypes() {
			static const std::vector<DocumentType> types = {
			  {".kv6", "KV6 voxel model", DocumentKind::Model, true},
			  {".2kv6", "2KV6 voxel scene", DocumentKind::Scene, false},
			  {".vxl", "VXL voxel map", DocumentKind::Map, false},
			};
			return types;
		}

		const DocumentType* FindDocumentType(const std::string& name) {
			for (const DocumentType& type : DocumentTypes()) {
				if (fs::HasExtension(name, type.extension))
					return &type;
			}
			return nullptr;
		}

		bool IsEditableDocument(const std::string& name) {
			const DocumentType* type = FindDocumentType(name);
			return type && type->editable;
		}

		const std::vector<std::string>& DocumentExtensions() {
			static const std::vector<std::string> extensions = [] {
				std::vector<std::string> list;
				for (const DocumentType& type : DocumentTypes())
					list.push_back(type.extension);
				return list;
			}();
			return extensions;
		}

		const std::string& DefaultExtension(DocumentKind kind) {
			// DocumentExtensions() lists the types in order, so entry i is the
			// extension of type i.
			const std::vector<DocumentType>& types = DocumentTypes();
			for (std::size_t i = 0; i < types.size(); i++) {
				if (types[i].kind == kind && types[i].editable)
					return DocumentExtensions()[i];
			}
			static const std::string none;
			return none;
		}

		std::string DocumentFileName(const std::string& name, DocumentKind kind) {
			return FindDocumentType(name) ? name : name + DefaultExtension(kind);
		}

		std::string UntitledFileName(DocumentKind kind) {
			return "untitled" + DefaultExtension(kind);
		}

		std::string DocumentFilterLabel() { return _Tr("Editor", "Voxel files"); }

		std::string UnsupportedDocumentHint() { return _Tr("Editor", "(not implemented)"); }

		std::string UnsupportedDocumentMessage() {
			std::string editable;
			for (const DocumentType& type : DocumentTypes()) {
				if (!type.editable)
					continue;
				if (!editable.empty())
					editable += ", ";
				editable += type.extension;
			}
			return _Tr("Editor",
			           "This file type is not supported yet. Only {0} files can be edited.",
			           editable);
		}
	} // namespace gui
} // namespace spades
