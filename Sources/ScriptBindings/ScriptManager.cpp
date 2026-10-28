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

#include "ScriptManager.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <Core/Strings.h>
#include <ScriptBindings/ScriptFunction.h>
#include <sstream>
#include <vector>

namespace spades {

	ScriptManager* ScriptManager::GetInstance() {
		SPADES_MARK_FUNCTION_DEBUG();
		static ScriptManager* m = new ScriptManager();
		return m;
	}

	namespace {
		// What MessageCallback keeps of the first error of a build.
		struct BuildError {
			std::string message;
			std::string section; // the script, as "/Skin/Foo.as"
		};
	} // namespace

	// `param` is null, or a `BuildError` that receives the first error.
	static void MessageCallback(const asSMessageInfo* msg, void* param) {
		SPADES_MARK_FUNCTION();
		const char* type = "ERR ";
		if (msg->type == asMSGTYPE_WARNING)
			type = "WARN";
		else if (msg->type == asMSGTYPE_INFORMATION)
			type = "INFO";
		SPLog("%s (%d, %d) : %s : %s\n", msg->section, msg->row, msg->col, type, msg->message);

		auto* firstError = static_cast<BuildError*>(param);
		if (firstError && firstError->message.empty() && msg->type == asMSGTYPE_ERROR) {
			firstError->message =
			  Format("{0} ({1}, {2}): {3}", msg->section, msg->row, msg->col, msg->message);
			firstError->section = msg->section;
		}
	}

	// Helper function to normalize paths (resolve .. and .)
	static std::string NormalizePath(const std::string& path) {
		std::vector<std::string> parts;
		std::stringstream ss(path);
		std::string part;

		while (std::getline(ss, part, '/')) {
			if (part == ".." && !parts.empty()) {
				parts.pop_back();
			} else if (!part.empty() && part != ".") {
				parts.push_back(part);
			}
		}

		std::string result = "/";
		for (size_t i = 0; i < parts.size(); i++) {
			if (i > 0) result += "/";
			result += parts[i];
		}
		return result;
	}

	// Callback function for handling script includes
	static int IncludeCallback(const char* include, const char* from, CScriptBuilder* builder, void* userParam) {
		SPADES_MARK_FUNCTION();

		std::string includePath = include;

		// Handle relative paths - resolve relative to the current file's directory
		if (includePath[0] != '/') {
			std::string fromPath = from;
			size_t lastSlash = fromPath.find_last_of('/');
			if (lastSlash != std::string::npos) {
				// Get the directory of the 'from' file and append the include path
				includePath = fromPath.substr(0, lastSlash + 1) + includePath;
			} else {
				// No directory in 'from', treat as root-relative
				includePath = "/" + includePath;
			}
		}

		// Normalize the path (resolve .. and .)
		includePath = NormalizePath(includePath);

		// Wildcard include ("dir/*.as"): pull in every .as in the directory, so mods
		// can add scripts (e.g. editor tools) by just dropping in a file. EnumFiles
		// merges paks and mod folders and returns a sorted, de-duplicated set, so the
		// result is deterministic and a mod file overrides a base file of the same name.
		if (includePath.find('*') != std::string::npos) {
			size_t slash = includePath.find_last_of('/');
			std::string dir = (slash == std::string::npos) ? "" : includePath.substr(0, slash);
			std::vector<std::string> names;
			try {
				names = FileManager::EnumFiles(("Scripts" + dir).c_str());
			} catch (const std::exception& ex) {
				SPLog("Failed to enumerate '%s':%s", dir.c_str(), ex.what());
				return -1;
			}
			for (const std::string& name : names) {
				if (name.size() < 3 || name.compare(name.size() - 3, 3, ".as") != 0)
					continue;
				std::string section = dir + "/" + name;
				std::string contents;
				try {
					contents = FileManager::ReadAllBytes(("Scripts" + section).c_str());
				} catch (const std::exception& ex) {
					SPLog("Failed to include '%s':%s", section.c_str(), ex.what());
					return -1;
				}
				SPLog("Loading script '%s'", section.c_str());
				if (builder->AddSectionFromMemory(section.c_str(), contents.c_str(),
				                                  (unsigned int)(contents.length()), 0) < 0)
					return -1;
			}
			return 0;
		}

		// path validation should be done by filesystem...
		std::string data;
		try {
			std::string fn = "Scripts";
			fn += includePath;
			data = FileManager::ReadAllBytes(fn.c_str());
		} catch (const std::exception& ex) {
			SPLog("Failed to include '%s':%s", includePath.c_str(), ex.what());
			return -1;
		}

		SPLog("Loading script '%s'", includePath.c_str());
		return builder->AddSectionFromMemory(includePath.c_str(), data.c_str(), (unsigned int)(data.length()), 0);
	}

	ScriptManager::ScriptManager() {
		SPADES_MARK_FUNCTION();

		SPLog("Creating script engine");
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);

		try {
			SPLog("Configuring");
			engine->SetEngineProperty(asEP_REQUIRE_ENUM_SCOPE, 1);
			engine->SetEngineProperty(asEP_DISALLOW_GLOBAL_VARS, 1);
			engine->SetMessageCallback(asFUNCTION(MessageCallback), 0, asCALL_CDECL);
			SPLog("Registering standard libray functions");
			RegisterScriptAny(engine);
			RegisterScriptWeakRef(engine);
			RegisterScriptHandle(engine);
			RegisterScriptMath(engine);
			RegisterScriptMathComplex(engine);
			RegisterScriptArray(engine, true);
			RegisterStdString(engine);
			RegisterStdStringUtils(engine);
			RegisterScriptDictionary(engine);

			SPLog("Registering APIs");
			engine->SetDefaultNamespace("");
			ScriptObjectRegistrar::RegisterAll(this, ScriptObjectRegistrar::PhaseObjectType);
			ScriptObjectRegistrar::RegisterAll(this, ScriptObjectRegistrar::PhaseGlobalFunction);
			ScriptObjectRegistrar::RegisterAll(this, ScriptObjectRegistrar::PhaseObjectMember);

			// A build error is kept, not raised: the engine is fine, and the scripts
			// can be built again on it once the mods at fault are left out.
			startupError = BuildModule("Client", startupErrorFile);
		} catch (...) {
			engine->Release();
			throw;
		}
	}

	std::string ScriptManager::BuildModule(const std::string& name, std::string& errorFile) {
		SPADES_MARK_FUNCTION();

		BuildError error;
		engine->SetMessageCallback(asFUNCTION(MessageCallback), &error, asCALL_CDECL);
		try {
			SPLog("Loading scripts");
			engine->SetDefaultNamespace("");
			CScriptBuilder builder;
			builder.SetIncludeCallback(IncludeCallback, NULL);
			if (builder.StartNewModule(engine, name.c_str()) < 0)
				SPRaise("Failed to create script module.");
			builder.DefineWord("CLIENT");

			std::string mainScript = FileManager::ReadAllBytes("Scripts/Main.as");
			if (builder.AddSectionFromMemory("/Main.as", mainScript.c_str(),
			                                 (unsigned int)(mainScript.length()), 0) < 0)
				SPRaise("Failed to load '/Main.as'.");

			SPLog("Building");
			if (builder.BuildModule() < 0)
				SPRaise("Failed to build at least one of the scripts.");
		} catch (const std::exception& ex) {
			if (error.message.empty())
				error.message = ex.what();
		}
		engine->SetMessageCallback(asFUNCTION(MessageCallback), 0, asCALL_CDECL);

		// The game calls into the module by these; it must have them all.
		if (error.message.empty())
			error.message = ScriptFunction::FindMissing(engine, engine->GetModule(name.c_str()));

		errorFile.clear();
		if (!error.message.empty()) {
			DiscardModule(name);
			if (!error.section.empty())
				errorFile = "Scripts" + error.section;
		}
		return error.message;
	}

	std::string ScriptManager::RebuildClientModule(std::string& errorFile) {
		SPADES_MARK_FUNCTION();

		std::string error = BuildModule("ClientNext", errorFile);
		if (!error.empty())
			return error;
		// What the old module made, if anything, keeps its code alive.
		DiscardModule("Client");
		engine->GetModule("ClientNext")->SetName("Client");
		generation++;
		startupError.clear();
		startupErrorFile.clear();
		SPLog("Scripts rebuilt");
		return {};
	}

	void ScriptManager::DiscardModule(const std::string& name) {
		if (engine->GetModule(name.c_str()))
			engine->DiscardModule(name.c_str());
	}

	static std::string ASErrorToString(int ret) {
		SPADES_MARK_FUNCTION();
		switch (ret) {
			case asSUCCESS: return "Success";
			case asERROR: return "Unknown script engine error";
			case asCONTEXT_NOT_FINISHED: return "Context not finished";
			case asCONTEXT_NOT_PREPARED: return "Context not prepared";
			case asINVALID_ARG: return "Invalid argument";
			case asNO_FUNCTION: return "No function";
			case asNOT_SUPPORTED: return "Not supported";
			case asINVALID_NAME: return "Invalid name";
			case asNAME_TAKEN: return "Name taken";
			case asINVALID_DECLARATION: return "Invalid declaration";
			case asINVALID_OBJECT: return "Invalid object";
			case asINVALID_TYPE: return "Invalid type";
			case asALREADY_REGISTERED: return "Already registered";
			case asMULTIPLE_FUNCTIONS: return "Multiple functions";
			case asNO_MODULE: return "No module";
			case asNO_GLOBAL_VAR: return "No global variable";
			case asINVALID_CONFIGURATION: return "Invalid configuration";
			case asINVALID_INTERFACE: return "Invalid interface";
			case asCANT_BIND_ALL_FUNCTIONS: return "Failed to bind all functions";
			case asLOWER_ARRAY_DIMENSION_NOT_REGISTERED:
				return "A lower array dimension is not registered";
			case asWRONG_CONFIG_GROUP: return "Wrong config group";
			case asCONFIG_GROUP_IS_IN_USE: return "Config group is in use";
			case asILLEGAL_BEHAVIOUR_FOR_TYPE: return "Illegal behaviour for the given type";
			case asWRONG_CALLING_CONV: return "Wrong call convention";
			case asBUILD_IN_PROGRESS: return "Build in progress";
			case asINIT_GLOBAL_VARS_FAILED: return "Failed to initialize global variables";
			case asOUT_OF_MEMORY: return "Out of memory";
			default: return "Unknown error code";
		}
	}

	void ScriptManager::CheckError(int ret) {
		if (ret < 0) {
			SPADES_MARK_FUNCTION();
			SPRaise("%s", ASErrorToString(ret).c_str());
		}
	}

	ScriptManager::~ScriptManager() {
		SPADES_MARK_FUNCTION();
		engine->Release();
	}

	ScriptContextHandle ScriptManager::GetContext() {
		SPADES_MARK_FUNCTION_DEBUG();
		if (contextFreeList.empty()) {
			std::lock_guard<std::recursive_mutex> _lock{contextMutex};
			// no free context; create one
			Context* ctx = new Context();
			ctx->obj = engine->CreateContext();
			ctx->refCount = 0;
			return ScriptContextHandle(ctx, this);
		} else {
			// get one
			Context* ctx = contextFreeList.front();
			contextFreeList.pop_front();
			SPAssert(ctx->refCount == 0);
			return ScriptContextHandle(ctx, this);
		}
	}

	ScriptContextHandle::ScriptContextHandle() : manager(NULL), obj(NULL) {}

	ScriptContextHandle::ScriptContextHandle(ScriptManager::Context* ctx, ScriptManager* manager)
	    : manager(manager), obj(ctx) {
		std::lock_guard<std::recursive_mutex> _lock{manager->contextMutex};
		ctx->refCount++;
	}

	ScriptContextHandle::ScriptContextHandle(const ScriptContextHandle& h)
	    : manager(h.manager), obj(h.obj) {
		std::lock_guard<std::recursive_mutex> _lock{manager->contextMutex};
		obj->refCount++;
	}

	ScriptContextHandle::~ScriptContextHandle() { Release(); }

	void ScriptContextHandle::Release() {
		if (obj) {
			std::lock_guard<std::recursive_mutex> _lock{manager->contextMutex};
			obj->refCount--;
			if (obj->refCount == 0) {
				// this context is no longer used;
				// add to freelist, without the object it returned last, which
				// would otherwise live on, with all it holds, until its next use
				obj->obj->Unprepare();
				manager->contextFreeList.push_back(obj);
			}

			obj = NULL;
			manager = NULL;
		}
	}

	void ScriptContextHandle::operator=(const spades::ScriptContextHandle& h) {
		if (h.obj == obj)
			return;
		Release();

		manager = h.manager;
		obj = h.obj;
		std::lock_guard<std::recursive_mutex> _lock{manager->contextMutex};
		obj->refCount++;
	}

	asIScriptContext* ScriptContextHandle::GetContext() const {
		SPADES_MARK_FUNCTION_DEBUG();
		SPAssert(obj != NULL);
		return obj->obj;
	}

	asIScriptContext* ScriptContextHandle::operator->() const {
		SPADES_MARK_FUNCTION_DEBUG();
		return GetContext();
	}

	void ScriptContextHandle::ExecuteChecked() {
		SPADES_MARK_FUNCTION();
		ScriptContextUtils(GetContext()).ExecuteChecked();
	}

	static std::map<std::string, ScriptObjectRegistrar*>* registrars = NULL;

	ScriptObjectRegistrar::ScriptObjectRegistrar(const std::string& name) : name(name) {
		if (!registrars)
			registrars = new std::map<std::string, ScriptObjectRegistrar*>();
		if (registrars->find(name) != registrars->end()) {
			SPLog("WARNING: there are more than one ScriptObjectRegistrar with name '%s'",
			      name.c_str());
		}
		(*registrars)[name] = this;
		std::fill(phaseDone, phaseDone + PhaseCount, false);
	}

	void ScriptObjectRegistrar::RegisterOne(const std::string& name, ScriptManager* manager,
	                                        Phase phase) {
		std::map<std::string, ScriptObjectRegistrar*>::iterator it;
		it = registrars->find(name);
		if (it == registrars->end()) {
			SPLog("WARNING: ScriptObjectRegistrar with name '%s' not found", name.c_str());
		} else {
			ScriptObjectRegistrar* r = it->second;
			if (r->phaseDone[(int)phase])
				return;
			r->phaseDone[(int)phase] = true;
			r->Register(manager, phase);
		}
	}

	void ScriptObjectRegistrar::RegisterAll(ScriptManager* manager, Phase phase) {
		std::map<std::string, ScriptObjectRegistrar*>::iterator it;
		for (it = registrars->begin(); it != registrars->end(); it++) {
			ScriptObjectRegistrar* r = it->second;
			if (r->phaseDone[(int)phase])
				continue;
			r->phaseDone[(int)phase] = true;
			r->Register(manager, phase);
		}
	}

#pragma mark - Context Utils

	ScriptContextUtils::ScriptContextUtils() : context(asGetActiveContext()) {}
	ScriptContextUtils::ScriptContextUtils(asIScriptContext* ctx) : context(ctx) {}

	void ScriptContextUtils::appendLocation(std::stringstream& ss, asIScriptFunction* func,
	                                        const char* secName, int line, int column) {
		ss << "[" << (secName ? secName : "(stub)") << ":" << line << "," << column << "] "
		   << func->GetDeclaration(true, true);
	}

	void ScriptContextUtils::ExecuteChecked() {
		SPADES_MARK_FUNCTION();
		int r = context->Execute();
		ScriptManager::CheckError(r);
		if (r == asEXECUTION_ABORTED) {
			SPRaise("Script execution aborted.");
		} else if (r == asEXECUTION_SUSPENDED) {
			SPRaise("Script execution suspended."); // TODO: shouldn't raise error?
		} else if (r == asEXECUTION_EXCEPTION) {
			const char* secName = NULL;
			int line = 0, column = 0;
			asIScriptFunction* func = context->GetExceptionFunction();
			line = context->GetExceptionLineNumber(&column, &secName);
			std::stringstream ss;
			ss << context->GetExceptionString() << " @ ";
			appendLocation(ss, func, secName, line, column);
			asUINT num = context->GetCallstackSize();
			for (asUINT n = 1; n < num; ++n) { // skip entry 0, that's the current / exception addr
				func = context->GetFunction(n);
				line = context->GetLineNumber(n, &column, &secName);
				ss << std::endl << " > ";
				appendLocation(ss, func, secName, line, column);
			}
			std::string tmp = ss.str();
			SPRaise(tmp.c_str());
		}
	}

	void ScriptContextUtils::SetNativeException(const std::exception& ex) {
		context->SetException(ex.what());
	}

#pragma mark - Some Script Functions

	class SomeScriptFunctionRegistrar : public ScriptObjectRegistrar {
	public:
		SomeScriptFunctionRegistrar() : ScriptObjectRegistrar("SomeScriptFunc") {}

		static void Raise(const std::string& str) {
			asGetActiveContext()->SetException(str.c_str());
		}

		static void Assert(bool cond) {
			if (!cond)
				Raise("Assertion failed");
		}

		static void NotImplemented() { Raise("Not implemented"); }

		virtual void Register(ScriptManager* manager, Phase phase) {
			asIScriptEngine* eng = manager->GetEngine();
			eng->SetDefaultNamespace("spades");
			switch (phase) {
				case PhaseGlobalFunction:
					eng->RegisterGlobalFunction("void Raise(const string& in)", asFUNCTION(Raise),
					                            asCALL_CDECL);
					eng->RegisterGlobalFunction("void Assert(bool cond)", asFUNCTION(Assert),
					                            asCALL_CDECL);
					eng->RegisterGlobalFunction("void NotImplemented()", asFUNCTION(NotImplemented),
					                            asCALL_CDECL);
					break;
				default: break;
			}
		}
	};

	static SomeScriptFunctionRegistrar ssfReg;
} // namespace spades
