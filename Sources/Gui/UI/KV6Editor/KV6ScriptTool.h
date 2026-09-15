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
class asIScriptFunction;

namespace spades {
	namespace gui {
		class SubToolRegistry;
		// Adapts a script object implementing the `EditorTool` script interface to a
		// C++ `EditorTool`, forwarding each callback into the script. The live
		// `IEditorContext` is handed to the script as the bound `EditorContext@`, and
		// pointer/key events are flattened to primitives so no event value type has
		// to cross the boundary.
		class ScriptEditorTool : public EditorTool {
		public:
			// Adopts `obj` (takes ownership of one reference; released on destruction).
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
			// Concrete tool methods, resolved once from the object's type (null if the
			// tool doesn't provide one).
			asIScriptFunction* fnActivate = nullptr;
			asIScriptFunction* fnDeactivate = nullptr;
			asIScriptFunction* fnPointer = nullptr;
			asIScriptFunction* fnKey = nullptr;
			asIScriptFunction* fnEscape = nullptr;
			asIScriptFunction* fnDraw = nullptr;
			std::string label;
		};

		// Discover every script class implementing the `EditorTool` interface in the
		// compiled module and register it with `reg` for the targets it declares via
		// `Targets()`. This is what makes a tool appear by just adding its script —
		// no C++ change. Safe to call when no scripts are present (registers nothing).
		void RegisterScriptTools(SubToolRegistry& reg);
	} // namespace gui
} // namespace spades
