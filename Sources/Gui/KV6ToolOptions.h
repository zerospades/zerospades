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

#include <string>
#include <vector>

namespace spades {
	namespace gui {
		// A single declarative tool option, rendered generically in the sub-toolbar.
		// Tools list the options they want and the editor draws/handles them, so a
		// new (or scripted) tool gets its UI for free without touching the toolbar
		// code. Options sharing a non-empty `group` are drawn together behind a
		// separator and a group label (e.g. the three "Mirror" toggles).
		struct ToolOption {
			enum class Type {
				Bool,  // a toggle button; value lives in `bvalue`
				Color  // the editor's current-colour swatch; opens the picker on click
				       // (its value is owned by the editor, not stored here)
			};

			std::string id;    // stable key, e.g. "mirror.x"
			std::string label; // short button text ("X"); empty for icon/swatch only
			std::string group; // shared group label ("Mirror"); empty = ungrouped
			Type type = Type::Bool;
			bool bvalue = false; // Bool value
		};

		// An ordered list of a tool's options.
		class ToolOptions {
		public:
			void AddBool(const std::string& id, const std::string& label,
			             const std::string& group = "", bool initial = false) {
				ToolOption o;
				o.id = id;
				o.label = label;
				o.group = group;
				o.type = ToolOption::Type::Bool;
				o.bvalue = initial;
				items.push_back(o);
			}
			void AddColor(const std::string& id, const std::string& group = "") {
				ToolOption o;
				o.id = id;
				o.group = group;
				o.type = ToolOption::Type::Color;
				items.push_back(o);
			}

			int Count() const { return int(items.size()); }
			ToolOption& At(int i) { return items[i]; }
			const ToolOption& At(int i) const { return items[i]; }

			bool GetBool(const std::string& id) const {
				for (const ToolOption& o : items)
					if (o.id == id)
						return o.bvalue;
				return false;
			}

		private:
			std::vector<ToolOption> items;
		};
	} // namespace gui
} // namespace spades
