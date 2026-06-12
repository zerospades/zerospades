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

#include "KV6ScriptTool.h"
#include "KV6EditorContext.h"

#include <ScriptBindings/ScriptFunction.h>
#include <ScriptBindings/ScriptManager.h>

namespace spades {
	namespace gui {
		namespace {
			// Prepare a context to call `fn` on the script tool object.
			ScriptContextHandle PrepareCall(asIScriptFunction* fn, asIScriptObject* obj) {
				ScriptContextHandle c = ScriptManager::GetInstance()->GetContext();
				c->Prepare(fn);
				c->SetObject(obj);
				return c;
			}
		} // namespace

		ScriptEditorTool::ScriptEditorTool(asIScriptObject* o) : obj(o) {
			if (obj == nullptr)
				return;
			obj->AddRef();

			// Resolve the tool's methods on its concrete type. Decls are parsed in the
			// `spades` namespace so the bound `EditorContext` type resolves.
			asITypeInfo* ti = obj->GetObjectType();
			asIScriptEngine* eng = ti->GetEngine();
			asIScriptModule* mod = ti->GetModule();
			eng->SetDefaultNamespace("spades");
			if (mod != nullptr)
				mod->SetDefaultNamespace("spades");

			fnActivate = ti->GetMethodByDecl("void OnActivate(EditorContext@)");
			fnDeactivate = ti->GetMethodByDecl("void OnDeactivate(EditorContext@)");
			fnPointer = ti->GetMethodByDecl("void OnPointer(EditorContext@, int, int, bool, bool, bool)");
			fnKey = ti->GetMethodByDecl("void OnKey(EditorContext@, string, bool)");
			fnEscape = ti->GetMethodByDecl("bool OnEscape(EditorContext@)");
			fnDraw = ti->GetMethodByDecl("void DrawScene(EditorContext@)");

			// Fetch the toolbar label once; it doesn't change at runtime.
			if (asIScriptFunction* fnLabel = ti->GetMethodByDecl("string Label()")) {
				try {
					ScriptContextHandle c = PrepareCall(fnLabel, obj);
					c.ExecuteChecked();
					label = *reinterpret_cast<std::string*>(c->GetReturnObject());
				} catch (...) {
					label.clear();
				}
			}
			if (label.empty())
				label = "Script";
		}

		ScriptEditorTool::~ScriptEditorTool() {
			if (obj != nullptr)
				obj->Release();
		}

		void ScriptEditorTool::OnActivate(IEditorContext& ed) {
			if (fnActivate == nullptr)
				return;
			ScriptContextHandle c = PrepareCall(fnActivate, obj);
			c->SetArgObject(0, &ed);
			c.ExecuteChecked();
		}

		void ScriptEditorTool::OnDeactivate(IEditorContext& ed) {
			if (fnDeactivate == nullptr)
				return;
			ScriptContextHandle c = PrepareCall(fnDeactivate, obj);
			c->SetArgObject(0, &ed);
			c.ExecuteChecked();
		}

		void ScriptEditorTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (fnPointer == nullptr)
				return;
			ScriptContextHandle c = PrepareCall(fnPointer, obj);
			c->SetArgObject(0, &ed);
			c->SetArgDWord(1, static_cast<asDWORD>(e.button));
			c->SetArgDWord(2, static_cast<asDWORD>(e.phase));
			c->SetArgByte(3, e.alt ? 1 : 0);
			c->SetArgByte(4, e.ctrl ? 1 : 0);
			c->SetArgByte(5, e.shift ? 1 : 0);
			c.ExecuteChecked();
		}

		void ScriptEditorTool::OnKey(IEditorContext& ed, const KeyInput& e) {
			if (fnKey == nullptr)
				return;
			std::string key = e.key; // must outlive the call (copied into the arg)
			ScriptContextHandle c = PrepareCall(fnKey, obj);
			c->SetArgObject(0, &ed);
			c->SetArgObject(1, &key);
			c->SetArgByte(2, e.IsDown() ? 1 : 0);
			c.ExecuteChecked();
		}

		bool ScriptEditorTool::OnEscape(IEditorContext& ed) {
			if (fnEscape == nullptr)
				return false;
			ScriptContextHandle c = PrepareCall(fnEscape, obj);
			c->SetArgObject(0, &ed);
			c.ExecuteChecked();
			return c->GetReturnByte() != 0;
		}

		void ScriptEditorTool::DrawScene(IEditorContext& ed) {
			if (fnDraw == nullptr)
				return;
			ScriptContextHandle c = PrepareCall(fnDraw, obj);
			c->SetArgObject(0, &ed);
			c.ExecuteChecked();
		}

		std::unique_ptr<EditorTool> MakeScriptSubTool(const std::string& factoryDecl, int mode) {
			try {
				ScriptFunction f(factoryDecl);
				ScriptContextHandle c = f.Prepare();
				c->SetArgDWord(0, static_cast<asDWORD>(mode));
				c.ExecuteChecked();
				asIScriptObject* o = reinterpret_cast<asIScriptObject*>(c->GetReturnObject());
				if (o == nullptr)
					return nullptr;
				// The adapter adds its own reference; the context drops its one when it
				// is recycled, so the tool keeps the object alive.
				return std::unique_ptr<EditorTool>(new ScriptEditorTool(o));
			} catch (...) {
				return nullptr;
			}
		}
	} // namespace gui
} // namespace spades
