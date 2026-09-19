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
#include "KV6SubToolRegistry.h"

#include <string>

#include <Core/Debug.h>
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

			// Instantiate a script class via its no-arg factory. Returns an object
			// owning one reference (caller releases or hands to ScriptEditorTool).
			asIScriptObject* CreateInstance(asIScriptFunction* factory) {
				if (factory == nullptr)
					return nullptr;
				try {
					ScriptContextHandle c = ScriptManager::GetInstance()->GetContext();
					if (c->Prepare(factory) < 0)
						return nullptr;
					c.ExecuteChecked();
					asIScriptObject* o = reinterpret_cast<asIScriptObject*>(c->GetReturnObject());
					if (o != nullptr)
						o->AddRef(); // keep it alive past the context's own reference
					return o;
				} catch (const std::exception& ex) {
					SPLog("Failed to instantiate editor tool script: %s", ex.what());
					return nullptr;
				}
			}
		} // namespace

		ScriptEditorTool::ScriptEditorTool(asIScriptObject* o) : obj(o) {
			if (obj == nullptr)
				return;

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
			fnEscapeLabel = ti->GetMethodByDecl("string EscapeLabel(EditorContext@)");
			fnEscape = ti->GetMethodByDecl("void OnEscape(EditorContext@)");
			fnHint = ti->GetMethodByDecl("string Hint(EditorContext@)");
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

		void ScriptEditorTool::OnActivate(IEditorContext& ed) { Call(fnActivate, ed); }

		void ScriptEditorTool::OnDeactivate(IEditorContext& ed) { Call(fnDeactivate, ed); }

		void ScriptEditorTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			Call(fnPointer, ed, [&](asIScriptContext& c) {
				c.SetArgDWord(1, static_cast<asDWORD>(e.button));
				c.SetArgDWord(2, static_cast<asDWORD>(e.phase));
				c.SetArgByte(3, e.alt ? 1 : 0);
				c.SetArgByte(4, e.ctrl ? 1 : 0);
				c.SetArgByte(5, e.shift ? 1 : 0);
			});
		}

		void ScriptEditorTool::OnKey(IEditorContext& ed, const KeyInput& e) {
			std::string key = e.key; // must outlive the call (copied into the arg)
			Call(fnKey, ed, [&](asIScriptContext& c) {
				c.SetArgObject(1, &key);
				c.SetArgByte(2, e.IsDown() ? 1 : 0);
			});
		}

		std::string ScriptEditorTool::EscapeLabel(IEditorContext& ed) {
			return CallForString(fnEscapeLabel, ed);
		}

		void ScriptEditorTool::OnEscape(IEditorContext& ed) { Call(fnEscape, ed); }

		std::string ScriptEditorTool::Hint(IEditorContext& ed) { return CallForString(fnHint, ed); }

		void ScriptEditorTool::DrawScene(IEditorContext& ed) { Call(fnDraw, ed); }

		void ScriptEditorTool::Call(asIScriptFunction* fn, IEditorContext& ed,
		                            const std::function<void(asIScriptContext&)>& setArgs,
		                            const std::function<void(asIScriptContext&)>& read) {
			if (fn == nullptr)
				return; // the tool leaves this one to its default
			ScriptContextHandle c = PrepareCall(fn, obj);
			c->SetArgObject(0, &ed);
			if (setArgs)
				setArgs(*c.GetContext());
			c.ExecuteChecked();
			if (read)
				read(*c.GetContext());
		}

		std::string ScriptEditorTool::CallForString(asIScriptFunction* fn, IEditorContext& ed) {
			std::string result;
			Call(fn, ed, nullptr, [&](asIScriptContext& c) {
				result = *reinterpret_cast<std::string*>(c.GetReturnObject());
			});
			return result;
		}

		void RegisterScriptTools(SubToolRegistry& reg) {
			asIScriptEngine* eng = ScriptManager::GetInstance()->GetEngine();
			asIScriptModule* mod = eng->GetModule("Client");
			if (mod == nullptr)
				return;
			eng->SetDefaultNamespace("spades");
			mod->SetDefaultNamespace("spades");

			asITypeInfo* iface = mod->GetTypeInfoByName("EditorTool");
			if (iface == nullptr)
				return; // no editor-tool scripts compiled in

			asUINT count = mod->GetObjectTypeCount();
			for (asUINT i = 0; i < count; i++) {
				asITypeInfo* ti = mod->GetObjectTypeByIndex(i);
				if (ti == nullptr || ti == iface || !ti->Implements(iface))
					continue;
				if ((ti->GetFlags() & asOBJ_ABSTRACT) != 0)
					continue; // a base for tools (EditorToolBase), not a tool

				std::string cn = ti->GetName();
				asIScriptFunction* factory = ti->GetFactoryByDecl((cn + "@ " + cn + "()").c_str());
				if (factory == nullptr)
					continue; // tools need a no-argument constructor

				// Which containers the tool wants (bit 0 = Draw, bit 1 = Select,
				// bit 2 = Paint); default to all of them if it doesn't say.
				int targets = 7;
				if (asIScriptFunction* targetsFn = ti->GetMethodByDecl("int Targets()")) {
					if (asIScriptObject* probe = CreateInstance(factory)) {
						try {
							ScriptContextHandle c = PrepareCall(targetsFn, probe);
							c.ExecuteChecked();
							targets = c->GetReturnDWord();
						} catch (...) {}
						probe->Release();
					}
				}

				// Register one fresh-instance factory per container the tool targets.
				auto registerFor = [&reg, factory](SubToolTarget target) {
					reg.Register(target, [factory] {
						asIScriptObject* o = CreateInstance(factory);
						return o ? std::unique_ptr<EditorTool>(new ScriptEditorTool(o))
						         : std::unique_ptr<EditorTool>();
					});
				};
				if ((targets & 1) != 0) registerFor(SubToolTarget::Draw);
				if ((targets & 2) != 0) registerFor(SubToolTarget::Select);
				if ((targets & 4) != 0) registerFor(SubToolTarget::Paint);
			}
		}
	} // namespace gui
} // namespace spades
