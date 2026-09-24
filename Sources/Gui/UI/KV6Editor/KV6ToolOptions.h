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
				Label, // a read-only text readout; the tool updates `label` each frame
				Action, // a one-shot button; clicking calls EditorTool::OnAction(id)
				// An editable number: a box that can be typed into, with arrows
				// stepping it by `step`. Reported through
				// EditorTool::OnOptionNumberChanged, while it is being typed and
				// again once the user has settled on it.
				Number
			};

			std::string id;    // stable key, e.g. "mirror.x"
			std::string label; // button text ("X"), or a readout's current text
			std::string group; // shared group label ("Mirror"); empty = ungrouped
			Type type = Type::Bool;
			bool bvalue = false;     // Bool value
			bool enabled = true;     // false greys a Bool or Action out (nothing to act on)
			float value = 0.0F;      // Number value; the tool refreshes it via SetNumber
			float step = 1.0F;       // what one press of a Number's arrows adds
			int decimals = 1;        // decimal places a Number is written with
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
			// A one-shot button: it holds no state, and a click is reported to the
			// tool as OnAction(id) rather than flipping a value.
			void AddAction(const std::string& id, const std::string& label,
			               const std::string& group = "") {
				ToolOption o;
				o.id = id;
				o.label = label;
				o.group = group;
				o.type = ToolOption::Type::Action;
				items.push_back(o);
			}
			// An editable number, labelled (an axis letter) and stepped by `step`.
			// The tool refreshes its value via SetNumber each frame, except while
			// the user is typing into it.
			void AddNumber(const std::string& id, const std::string& label,
			               const std::string& group = "", float step = 1.0F,
			               int decimals = 1) {
				ToolOption o;
				o.id = id;
				o.label = label;
				o.group = group;
				o.type = ToolOption::Type::Number;
				o.step = step;
				o.decimals = decimals;
				items.push_back(o);
			}
			void SetNumber(const std::string& id, float value) {
				if (ToolOption* o = Find(id))
					o->value = value;
			}
			// A read-only readout; the tool refreshes its text via SetLabel each frame.
			void AddLabel(const std::string& id, const std::string& group = "") {
				ToolOption o;
				o.id = id;
				o.group = group;
				o.type = ToolOption::Type::Label;
				items.push_back(o);
			}
			void SetLabel(const std::string& id, const std::string& text) {
				if (ToolOption* o = Find(id))
					o->label = text;
			}
			void SetEnabled(const std::string& id, bool enabled) {
				if (ToolOption* o = Find(id))
					o->enabled = enabled;
			}

			/** The option with `id`, or null if there is none. */
			ToolOption* Find(const std::string& id) {
				for (ToolOption& o : items)
					if (o.id == id)
						return &o;
				return nullptr;
			}

			int Count() const { return int(items.size()); }
			ToolOption& At(int i) { return items[i]; }
			const ToolOption& At(int i) const { return items[i]; }

			void SetBool(const std::string& id, bool value) {
				if (ToolOption* o = Find(id))
					o->bvalue = value;
			}

		private:
			std::vector<ToolOption> items;
		};
	} // namespace gui
} // namespace spades
