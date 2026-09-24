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

namespace spades {
	namespace gui {
		namespace {
			const char* const kAxisOption[3] = {"pivot.x", "pivot.y", "pivot.z"};
			const char* const kAxisLabel[3] = {"X", "Y", "Z"};
		} // namespace

		// The pivot is a point anywhere, on no grid: it takes every step, finest first.
		PivotTool::PivotTool()
		    : GizmoTool(std::unique_ptr<GizmoSubTool>(new PivotGizmoSubTool()), {0.1F, 0.5F, 1.0F},
		                0.0F) {
			// The position is the tool: three boxes to type it into, which are
			// also what shows where the pivot is. The gizmo drags the same value.
			for (int a = 0; a < 3; a++)
				options.AddNumber(kAxisOption[a], kAxisLabel[a], "Pivot", 1.0F, kDecimals);
			AddSnapOptions();
		}

		int PivotTool::AxisOf(const std::string& id) {
			for (int a = 0; a < 3; a++)
				if (id == kAxisOption[a])
					return a;
			return -1;
		}

		void PivotTool::UpdateOptions(IEditorContext& ed) {
			GizmoTool::UpdateOptions(ed);
			// Whatever moved the pivot — a gizmo drag, undo, a box — the boxes
			// show where it is. The bar leaves the one being typed into alone.
			const Vector3 p = ed.GetPivot();
			const float axis[3] = {p.x, p.y, p.z};
			for (int a = 0; a < 3; a++)
				options.SetNumber(kAxisOption[a], axis[a]);
		}

		void PivotTool::OnOptionNumberChanged(IEditorContext& ed, const std::string& id,
		                                      float value, bool committed) {
			const int a = AxisOf(id);
			if (a < 0)
				return;
			// The other two axes come from the pivot as it stands, which is the
			// previewed one while a box is being typed into: typing X then Y
			// keeps both, rather than the second putting the first back.
			Vector3 p = ed.GetPivot();
			(a == 0 ? p.x : a == 1 ? p.y : p.z) = value;
			if (committed)
				ed.SetPivot(p);
			else
				ed.PreviewPivot(p);
		}
	} // namespace gui
} // namespace spades
