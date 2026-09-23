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

#include "ModelFileTypes.h"

#include <Core/LocalFileSystem.h>
#include <Core/Strings.h>

namespace spades {
	namespace gui {
		namespace fs = LocalFileSystem;

		namespace {
			/** One model file type, and whether the editor can open it yet. A type
			 *  that cannot be opened is still listed: a model the editor will
			 *  eventually read should be visible where the others are, rather than
			 *  leaving the player wondering where their file went. */
			struct ModelFileType {
				const char* extension;
				bool editable;
			};

			const ModelFileType kModelFileTypes[] = {
			  {".kv6", true},
			  {".2kv6", false},
			  {".vxl", false},
			};

			const ModelFileType* FindType(const std::string& name) {
				for (const ModelFileType& type : kModelFileTypes) {
					if (fs::HasExtension(name, type.extension))
						return &type;
				}
				return nullptr;
			}
		} // namespace

		const std::vector<std::string>& KV6ModelExtensions() {
			static const std::vector<std::string> extensions = [] {
				std::vector<std::string> list;
				for (const ModelFileType& type : kModelFileTypes)
					list.push_back(type.extension);
				return list;
			}();
			return extensions;
		}

		const std::string& KV6DocumentExtension() {
			// The first editable type is what a new document is written as.
			static const std::string extension = [] {
				for (const ModelFileType& type : kModelFileTypes) {
					if (type.editable)
						return std::string(type.extension);
				}
				return std::string();
			}();
			return extension;
		}

		bool KV6IsEditable(const std::string& name) {
			const ModelFileType* type = FindType(name);
			return type && type->editable;
		}

		std::string KV6DocumentFileName(const std::string& name) {
			return FindType(name) ? name : name + KV6DocumentExtension();
		}

		std::string KV6UntitledFileName() { return "untitled" + KV6DocumentExtension(); }

		std::string KV6ModelFilterLabel() { return _Tr("KV6Editor", "Voxel models"); }

		std::string KV6UnsupportedHint() { return _Tr("KV6Editor", "(not implemented)"); }

		std::string KV6UnsupportedMessage() {
			return _Tr("KV6Editor",
					   "This file type is not supported yet. Only {0} files can be edited.",
					   KV6DocumentExtension());
		}
	} // namespace gui
} // namespace spades
