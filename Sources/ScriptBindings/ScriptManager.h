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
#include <cstdint>
#include <list>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace spades {

	class ScriptContextHandle;

	/** The scripts don't build. */
	class ScriptBuildError : public std::runtime_error {
		std::string file;

	public:
		ScriptBuildError(const std::string& message, std::string file)
		    : std::runtime_error(message), file(std::move(file)) {}
		/** The script at fault, as "Scripts/Skin/Foo.as", or "" if none is. */
		const std::string& GetFile() const { return file; }
	};

	class ScriptManager {
		friend class ScriptContextHandle;
		struct Context {
			asIScriptContext* obj;
			int refCount;
		};
		std::recursive_mutex contextMutex;
		std::list<Context*> contextFreeList;

		asIScriptEngine* engine;
		std::uint64_t generation = 1;
		std::string startupError, startupErrorFile;

		ScriptManager();
		~ScriptManager();

	public:
		static ScriptManager* GetInstance();

		static void CheckError(int);

		/**
		 * Builds `Scripts/Main.as`, and what it includes, as the files are seen
		 * now, into the module `name`, and checks it has every function the game
		 * looks up. Returns the first error, or ""; a module with an error is
		 * dropped, and `errorFile` names the script at fault, if one is.
		 */
		std::string BuildModule(const std::string& name, std::string& errorFile);
		void DiscardModule(const std::string& name);

		/**
		 * Builds the scripts as mounted now into "Client", in place of the module
		 * there. Returns the first error, or ""; on an error "Client" stays.
		 */
		std::string RebuildClientModule(std::string& errorFile);
		/**
		 * Why "Client" didn't build when the engine started, or "" if it did;
		 * `file` names the script at fault, if one is. Rebuild it once the cause
		 * is gone.
		 */
		std::string GetStartupError(std::string& file) const {
			file = startupErrorFile;
			return startupError;
		}

		/** Changes each time "Client" is rebuilt. */
		std::uint64_t GetGeneration() const { return generation; }

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