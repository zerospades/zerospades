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

#include "KV6ToolRegistry.h"

#include "KV6DrawTool.h"
#include "KV6PaintTool.h"
#include "KV6PivotTool.h"
#include "KV6SelectTool.h"

namespace spades {
	namespace gui {
		ToolRegistry& ToolRegistry::Instance() {
			static ToolRegistry registry;
			static bool seeded = false;
			if (!seeded) {
				seeded = true;
				// Built-in tools, in toolbar order. Seeding here (rather than via
				// static initialisers in each tool's TU) keeps the order deterministic.
				registry.Register([] { return std::unique_ptr<EditorTool>(new PivotTool()); });
				registry.Register([] { return std::unique_ptr<EditorTool>(new DrawTool()); });
				registry.Register([] { return std::unique_ptr<EditorTool>(new PaintTool()); });
				registry.Register([] { return std::unique_ptr<EditorTool>(new SelectTool()); });
			}
			return registry;
		}

		void ToolRegistry::Register(Factory f) { factories.push_back(std::move(f)); }

		void ToolRegistry::BuildAll(std::vector<std::unique_ptr<EditorTool>>& out) const {
			out.clear();
			out.reserve(factories.size());
			for (const Factory& make : factories)
				out.push_back(make());
		}
	} // namespace gui
} // namespace spades
