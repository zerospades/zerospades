/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include <set>

#include "ScriptFunction.h"
#include <Core/Debug.h>
#include <Core/Strings.h>

namespace spades {

	namespace {
		// Every ScriptFunction, so a new module can be checked against them all.
		// Scripts only run on the main thread. Never destroyed: static
		// ScriptFunctions in other files may outlive any static here.
		std::set<ScriptFunction*>& GetFunctions() {
			static auto* functions = new std::set<ScriptFunction*>();
			return *functions;
		}
	} // namespace

	asIScriptFunction* ScriptFunction::Find(asIScriptEngine* eng, asIScriptModule* module,
	                                        std::string& error) const {
		eng->SetDefaultNamespace("spades");
		module->SetDefaultNamespace("spades");
		if (type.empty()) {
			asIScriptFunction* f = eng->GetGlobalFunctionByDecl(decl.c_str());
			if (!f)
				f = module->GetFunctionByDecl(decl.c_str());
			if (!f)
				error = Format("Script function with signature '{0}' not found.", decl);
			return f;
		}

		asITypeInfo* typ = eng->GetTypeInfoByName(type.c_str());
		if (!typ)
			typ = module->GetTypeInfoByName(type.c_str());
		if (!typ) {
			error = Format("Script type '{0}' not found.", type);
			return nullptr;
		}
		asIScriptFunction* f = typ->GetMethodByDecl(decl.c_str());
		if (!f)
			error = Format("Script method of '{0}' with signature '{1}' not found.", type, decl);
		return f;
	}

	void ScriptFunction::Load(asIScriptEngine* eng) {
		// Usually a static: look the function up again once "Client" is rebuilt.
		std::uint64_t generation = ScriptManager::GetInstance()->GetGeneration();
		if (eng != lastEngine || generation != loadedGeneration) {
			func = NULL;
			lastEngine = eng;
			loadedGeneration = generation;
		}
		if (func == NULL) {
			std::string error;
			func = Find(eng, eng->GetModule("Client"), error);
			if (!func)
				SPRaise("%s", error.c_str());
		}
	}

	std::string ScriptFunction::FindMissing(asIScriptEngine* eng, asIScriptModule* module) {
		for (ScriptFunction* function : GetFunctions()) {
			std::string error;
			if (!function->Find(eng, module, error))
				return error;
		}
		return {};
	}

	ScriptFunction::ScriptFunction(const std::string& decl)
	    : decl(decl), type(), lastEngine(NULL), loadedGeneration(0), func(NULL) {
		GetFunctions().insert(this);
	}

	ScriptFunction::ScriptFunction(const std::string& type, const std::string& decl)
	    : decl(decl), type(type), lastEngine(NULL), loadedGeneration(0), func(NULL) {
		GetFunctions().insert(this);
	}

	ScriptFunction::~ScriptFunction() {
		GetFunctions().erase(this);
	}

	ScriptContextHandle ScriptFunction::Prepare() {
		ScriptContextHandle ctx = ScriptManager::GetInstance()->GetContext();
		Load(ctx.GetContext()->GetEngine());

		ctx->Prepare(func);
		return ctx;
	}

	ScriptContextHandle ScriptFunction::Prepare(const std::string& module) {
		if (module == "Client")
			return Prepare();

		// A skin's module: looked up each time, as players are only made at spawn.
		ScriptContextHandle ctx = ScriptManager::GetInstance()->GetContext();
		asIScriptEngine* eng = ctx.GetContext()->GetEngine();
		asIScriptModule* mod = eng->GetModule(module.c_str());
		if (!mod)
			SPRaise("Script module '%s' not found.", module.c_str());
		std::string error;
		asIScriptFunction* f = Find(eng, mod, error);
		if (!f)
			SPRaise("%s", error.c_str());
		ctx->Prepare(f);
		return ctx;
	}

} // namespace spades
