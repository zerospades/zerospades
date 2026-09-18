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

#include "KV6PivotTool.h"
#include "KV6EditorContext.h"

#include <cstdio>

namespace spades {
	namespace gui {
		namespace {
			const char* const kSetOption = "pivot.set";
			const char* const kReadoutOption = "pivot.readout";
		} // namespace

		PivotTool::PivotTool() {
			subs.push_back(std::unique_ptr<EditorTool>(new PivotGizmoSubTool()));
			options.AddAction(kSetOption, "Set...");
			options.AddLabel(kReadoutOption);
		}

		void PivotTool::UpdateOptions(IEditorContext& ed) {
			Vector3 p = ed.GetPivot();
			char buf[80];
			std::snprintf(buf, sizeof(buf), "Pivot  %.1f, %.1f, %.1f", p.x, p.y, p.z);
			options.SetLabel(kReadoutOption, buf);
		}

		void PivotTool::OnAction(IEditorContext& ed, const std::string& id) {
			if (id == kSetOption)
				ed.BeginPivotEntry();
		}
	} // namespace gui
} // namespace spades
