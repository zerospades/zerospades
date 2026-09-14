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
#include <vector>

namespace spades {
	namespace gui {
		class EditorTool;

		/**
		 * The set of editor tools, as factories.
		 *
		 * The editor builds its toolbar from here instead of naming concrete tool
		 * classes, so the list of available tools is data, not code. Built-in tools
		 * seed the registry in registration (= toolbar) order; later this is where
		 * script-defined tools will append, with no change to the editor.
		 *
		 * `BuildAll` makes fresh instances, so every editor view gets its own tools
		 * (and their per-tool option state).
		 */
		class ToolRegistry {
		public:
			using Factory = std::function<std::unique_ptr<EditorTool>()>;

			// The shared registry, seeded with the built-in tools on first use.
			static ToolRegistry& Instance();

			// Append a tool factory; registration order is toolbar order.
			void Register(Factory f);
			// Instantiate every registered tool into `out` (cleared first).
			void BuildAll(std::vector<std::unique_ptr<EditorTool>>& out) const;
			int Count() const { return int(factories.size()); }

		private:
			std::vector<Factory> factories;
		};
	} // namespace gui
} // namespace spades
