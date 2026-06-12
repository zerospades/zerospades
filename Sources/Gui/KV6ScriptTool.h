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

#include <memory>
#include <string>

#include "KV6EditorTool.h"

class asIScriptObject;

namespace spades {
	namespace gui {
		// Adapts a script object implementing the `EditorTool` script interface to a
		// C++ `EditorTool`, forwarding each callback into the script. The live
		// `IEditorContext` is handed to the script as the bound `EditorContext@`, and
		// pointer/key events are flattened to primitives so no event value type has
		// to cross the boundary.
		class ScriptEditorTool : public EditorTool {
		public:
			// Adopts `obj` (adds a reference; released on destruction).
			explicit ScriptEditorTool(asIScriptObject* obj);
			~ScriptEditorTool() override;

			const char* Label() const override { return label.c_str(); }
			void OnActivate(IEditorContext&) override;
			void OnDeactivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void OnKey(IEditorContext&, const KeyInput&) override;
			bool OnEscape(IEditorContext&) override;
			void DrawScene(IEditorContext&) override;

		private:
			asIScriptObject* obj;
			std::string label;
		};

		// Build a script-backed tool by calling a global script factory
		// `EditorTool@ <factory>(int)` with `mode`. Returns null if the script is
		// unavailable or the call fails, so the editor simply omits the tool rather
		// than breaking.
		std::unique_ptr<EditorTool> MakeScriptSubTool(const std::string& factoryDecl, int mode);
	} // namespace gui
} // namespace spades
