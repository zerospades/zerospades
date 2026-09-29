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
#include <memory>
#include <string>

#include "VoxelTool.h"

class asIScriptObject;
class asIScriptFunction;
class asIScriptContext;

namespace spades {
	namespace gui {
		class SubToolRegistry;
		// Adapts a script object implementing the `EditorTool` script interface to a
		// C++ `VoxelTool`, forwarding each callback into the script. The live
		// `IVoxelEditContext` is handed to the script as the bound `EditorContext@`, and
		// pointer/key events are flattened to primitives so no event value type has
		// to cross the boundary.
		class ScriptEditorTool : public VoxelTool {
		public:
			// Adopts `obj` (takes ownership of one reference; released on destruction).
			explicit ScriptEditorTool(asIScriptObject* obj);
			~ScriptEditorTool() override;

			const char* Label() const override { return label.c_str(); }
			void OnActivate(IVoxelEditContext&) override;
			void OnDeactivate(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			void OnKey(IVoxelEditContext&, const KeyInput&) override;
			std::string EscapeLabel(IVoxelEditContext&) override;
			void OnEscape(IVoxelEditContext&) override;
			std::string Hint(IVoxelEditContext&) override;
			void DrawScene(IVoxelEditContext&) override;

		private:
			asIScriptObject* obj;
			// Runs `fn` on the tool with the editor as its first argument;
			// `setArgs` sets the others and `read` takes the result. A method the
			// tool does not have is skipped, leaving its default.
			void Call(asIScriptFunction* fn, IVoxelEditContext& ed,
			          const std::function<void(asIScriptContext&)>& setArgs = nullptr,
			          const std::function<void(asIScriptContext&)>& read = nullptr);
			// Call for a method returning a string; empty when skipped.
			std::string CallForString(asIScriptFunction* fn, IVoxelEditContext& ed);
			// Concrete tool methods, resolved once from the object's type (null if the
			// tool doesn't provide one).
			asIScriptFunction* fnActivate = nullptr;
			asIScriptFunction* fnDeactivate = nullptr;
			asIScriptFunction* fnPointer = nullptr;
			asIScriptFunction* fnKey = nullptr;
			asIScriptFunction* fnEscapeLabel = nullptr;
			asIScriptFunction* fnEscape = nullptr;
			asIScriptFunction* fnHint = nullptr;
			asIScriptFunction* fnDraw = nullptr;
			std::string label;
		};

		// Discover every script class implementing the `EditorTool` script interface in the
		// compiled module and register it with `reg` for the targets it declares via
		// `Targets()`. This is what makes a tool appear by just adding its script —
		// no C++ change. Safe to call when no scripts are present (registers nothing).
		void RegisterScriptTools(SubToolRegistry& reg);
	} // namespace gui
} // namespace spades
