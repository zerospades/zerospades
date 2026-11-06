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

#include "ScriptManager.h"
#include <cstdint>
#include <string>

namespace spades {
	class ScriptFunction {
		std::string decl, type;
		asIScriptEngine* lastEngine;
		std::uint64_t loadedGeneration; // ScriptManager::GetGeneration() of `func`
		asIScriptFunction* func;
		void Load(asIScriptEngine*);
		/** This function in `module`, or null with `error` saying why. */
		asIScriptFunction* Find(asIScriptEngine*, asIScriptModule* module,
		                        std::string& error) const;

	public:
		ScriptFunction(const std::string& decl);
		ScriptFunction(const std::string& objectTypeName, const std::string& decl);
		~ScriptFunction();
		ScriptFunction(const ScriptFunction&) = delete;
		void operator=(const ScriptFunction&) = delete;

		ScriptContextHandle Prepare();
		/** `Prepare`, with the function from the module `module` rather than "Client". */
		ScriptContextHandle Prepare(const std::string& module);

		/** Why `module` lacks a function some ScriptFunction looks up, or "". */
		static std::string FindMissing(asIScriptEngine*, asIScriptModule* module);
	};
} // namespace spades