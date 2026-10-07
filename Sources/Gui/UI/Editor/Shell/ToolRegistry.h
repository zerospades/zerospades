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
#include <string>
#include <utility>
#include <vector>

namespace spades {
	namespace gui {
		/** A tool as the editor holds it: the instance, and its toolbar button's place. */
		template <class Tool> struct BasicToolSlot {
			std::unique_ptr<Tool> tool;
			// Names the tool for the toolbar, whatever its label or position.
			std::string id;
			// Buttons of one group sit together, a separator apart from the next.
			int group = 0;
			// The key that activates the tool; empty for none.
			std::string hotKey;
		};

		/**
		 * An editor's set of tools, as factories.
		 *
		 * The editor builds its toolbar from here instead of naming concrete tool
		 * classes, so the list of available tools is data, not code. Each kind of
		 * editor keeps one, seeded with its tools in registration (= toolbar)
		 * order.
		 *
		 * `BuildAll` makes fresh instances, so every editor view gets its own tools
		 * (and their per-tool option state).
		 */
		template <class Tool> class BasicToolRegistry {
		public:
			using Factory = std::function<std::unique_ptr<Tool>()>;
			using Slot = BasicToolSlot<Tool>;

			// Append a tool factory; registration order is toolbar order.
			void Register(Factory f, const std::string& id, int group, const std::string& hotKey) {
				entries.push_back({std::move(f), id, group, hotKey});
			}

			// Instantiate every registered tool into `out` (cleared first).
			void BuildAll(std::vector<Slot>& out) const {
				out.clear();
				out.reserve(entries.size());
				for (const Entry& entry : entries) {
					Slot slot;
					slot.tool = entry.make();
					slot.id = entry.id;
					slot.group = entry.group;
					slot.hotKey = entry.hotKey;
					out.push_back(std::move(slot));
				}
			}

		private:
			struct Entry {
				Factory make;
				std::string id;
				int group;
				std::string hotKey;
			};
			std::vector<Entry> entries;
		};
	} // namespace gui
} // namespace spades
