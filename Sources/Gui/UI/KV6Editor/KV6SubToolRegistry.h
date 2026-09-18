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

		// Which built-in container a registered sub-tool belongs to.
		enum class SubToolTarget { Draw, Select, Paint };

		/**
		 * Sub-tools contributed to the built-in Draw / Select containers, as
		 * factories.
		 *
		 * The same shape tool (e.g. a script Cylinder) registers once per target,
		 * baking in the action it should perform there (fill for Draw, select for
		 * Select). `BuildFor` is called by each container's constructor to append its
		 * extra sub-tools, so script tools join the toolbar without the containers
		 * naming them.
		 */
		class SubToolRegistry {
		public:
			using Factory = std::function<std::unique_ptr<EditorTool>()>;

			// The shared registry, seeded with the built-in script sub-tools.
			static SubToolRegistry& Instance();

			void Register(SubToolTarget target, Factory f);
			// Append every sub-tool registered for `target` to `out`. Factories that
			// fail (e.g. their script is unavailable) yield null and are skipped.
			void BuildFor(SubToolTarget target, std::vector<std::unique_ptr<EditorTool>>& out) const;

		private:
			struct Entry {
				SubToolTarget target;
				Factory make;
			};
			std::vector<Entry> entries;
		};
	} // namespace gui
} // namespace spades
