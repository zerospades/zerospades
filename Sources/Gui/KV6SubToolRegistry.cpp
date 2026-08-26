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

#include "KV6SubToolRegistry.h"

#include "KV6EditorTool.h"
#include "KV6ScriptTool.h"

namespace spades {
	namespace gui {
		SubToolRegistry& SubToolRegistry::Instance() {
			static SubToolRegistry registry;
			static bool seeded = false;
			if (!seeded) {
				seeded = true;
				// Every script tool implementing the EditorTool interface registers
				// itself for the targets it declares — no tool is named here.
				RegisterScriptTools(registry);
			}
			return registry;
		}

		void SubToolRegistry::Register(SubToolTarget target, Factory f) {
			entries.push_back({target, std::move(f)});
		}

		void SubToolRegistry::BuildFor(SubToolTarget target,
		                               std::vector<std::unique_ptr<EditorTool>>& out) const {
			for (const Entry& e : entries) {
				if (e.target != target)
					continue;
				if (std::unique_ptr<EditorTool> t = e.make())
					out.push_back(std::move(t));
			}
		}
	} // namespace gui
} // namespace spades
