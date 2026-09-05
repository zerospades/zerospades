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

#pragma once

// Configure AngelScript string addon to use property accessors
// This is required for compatibility with ZeroSpades scripts that access string.length as a property
#define AS_USE_ACCESSORS 1

#include <AngelScript/include/angelscript.h>

// Calling conventions used by every binding registration below.
//
// Every target we build (x86/x64, macOS/Linux/Windows ARM64) provides AngelScript's
// native calling conventions, so these map straight onto them.
//
// These must stay native as long as registrations pass asFUNCTION()/asMETHOD():
// DetectCallingConvention() (as_callfunc.cpp) rejects a plain function pointer or a
// class-method pointer combined with asCALL_GENERIC and returns asWRONG_CALLING_CONV.
// A genuine AS_MAX_PORTABILITY build would additionally have to route every
// registration through the WRAP_FN/WRAP_MFN macros in addons/aswrappedcall.h, which
// emit real void(asIScriptGeneric*) thunks.
#define SPADES_ASCC_CDECL asCALL_CDECL
#define SPADES_ASCC_THISCALL asCALL_THISCALL
#define SPADES_ASCC_CDECL_OBJLAST asCALL_CDECL_OBJLAST
#include <AngelScript/addons/scriptany.h>
#include <AngelScript/addons/scriptarray.h>
#include <AngelScript/addons/scriptbuilder.h>
#include <AngelScript/addons/scriptdictionary.h>
#include <AngelScript/addons/scripthandle.h>
#include <AngelScript/addons/scripthelper.h>
#include <AngelScript/addons/scriptmath.h>
#include <AngelScript/addons/scriptmathcomplex.h>
#include <AngelScript/addons/scriptstdstring.h>
#include <AngelScript/addons/weakref.h>
#include <list>
#include <mutex>

namespace spades {

	class ScriptContextHandle;

	class ScriptManager {
		friend class ScriptContextHandle;
		struct Context {
			asIScriptContext* obj;
			int refCount;
		};
		std::recursive_mutex contextMutex;
		std::list<Context*> contextFreeList;

		asIScriptEngine* engine;

		ScriptManager();
		~ScriptManager();

	public:
		static ScriptManager* GetInstance();

		static void CheckError(int);

		asIScriptEngine* GetEngine() const { return engine; }

		ScriptContextHandle GetContext();
	};

	class ScriptContextUtils {
		asIScriptContext* context;

		void appendLocation(std::stringstream& ss, asIScriptFunction* func, const char* secName,
		                    int line, int column);

	public:
		ScriptContextUtils();
		ScriptContextUtils(asIScriptContext*);
		void ExecuteChecked();
		void SetNativeException(const std::exception&);
	};

	class ScriptContextHandle {
		ScriptManager* manager;
		ScriptManager::Context* obj;

		void Release();

	public:
		ScriptContextHandle();
		ScriptContextHandle(ScriptManager::Context*, ScriptManager* manager);
		ScriptContextHandle(const ScriptContextHandle&);
		~ScriptContextHandle();
		void operator=(const ScriptContextHandle&);
		asIScriptContext* GetContext() const;
		asIScriptContext* operator->() const;

		ScriptManager* GetManager() const { return manager; }

		void ExecuteChecked();
	};

	class ScriptObjectRegistrar {
	public:
		enum Phase { PhaseObjectType, PhaseGlobalFunction, PhaseObjectMember, PhaseCount };
		ScriptObjectRegistrar(const std::string& name);
		virtual void Register(ScriptManager* manager, Phase) = 0;

		static void RegisterOne(const std::string& name, ScriptManager* manager, Phase);
		static void RegisterAll(ScriptManager* manager, Phase);

	private:
		bool phaseDone[PhaseCount];
		std::string name;
	};

} // namespace spades