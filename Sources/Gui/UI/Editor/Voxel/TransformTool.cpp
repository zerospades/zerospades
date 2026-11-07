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

#include "TransformTool.h"
#include "VoxelEditContext.h"

#include <cmath>

namespace spades {
	namespace gui {
		namespace {
			const char* const kAboutSelectionOption = "transform.about.selection";
			const char* const kAboutPivotOption = "transform.about.pivot";
			const char* const kMoveOption[3] = {"transform.move.x", "transform.move.y",
			                                    "transform.move.z"};
			const char* const kTurnOption[3] = {"transform.turn.x", "transform.turn.y",
			                                    "transform.turn.z"};
			const char* const kAxisLabel[3] = {"X", "Y", "Z"};
			// Voxels land on voxels, so a turn is a right angle or nothing.
			const char* const kTurnGroup = "Turn 90";

			// Which of `ids` `id` is, or -1 if it is none of them.
			int AxisOf(const char* const (&ids)[3], const std::string& id) {
				for (int a = 0; a < 3; a++)
					if (id == ids[a])
						return a;
				return -1;
			}
		} // namespace

		// Voxels only ever land on voxels: they move by whole ones and always sit
		// on that grid, so neither a finer step nor Snap to Grid applies.
		TransformTool::TransformTool()
		    : GizmoTool(std::unique_ptr<GizmoSubTool>(new TransformSubTool()), {1.0F}, 1.0F) {
			// Whole voxels: the boxes have nothing to say after the decimal point.
			for (int a = 0; a < 3; a++)
				options.AddNumber(kMoveOption[a], kAxisLabel[a], "Move", 1.0F, 0);
			for (int a = 0; a < 3; a++)
				options.AddAction(kTurnOption[a], kAxisLabel[a], kTurnGroup);
			options.AddBool(kAboutSelectionOption, "Selection", "Turn about");
			options.AddBool(kAboutPivotOption, "Pivot", "Turn about");
			AddSnapOptions();
		}

		void TransformTool::UpdateOptions(IVoxelEditContext& ed) {
			GizmoTool::UpdateOptions(ed);
			// The turn centre is the editor's setting; the pair only shows it.
			const bool aboutPivot = ed.TurnsAboutModelPivot();
			options.SetBool(kAboutSelectionOption, !aboutPivot);
			options.SetBool(kAboutPivotOption, aboutPivot);
			// The Move boxes are where the voxels are, so they follow a gizmo
			// drag, a paste and undo alike. With nothing to move there is no
			// centre to show, and the boxes and the turns grey out.
			IntVector3 centre;
			const bool movable = ed.TransformPivot(centre);
			const int at[3] = {centre.x, centre.y, centre.z};
			for (int a = 0; a < 3; a++) {
				options.SetEnabled(kMoveOption[a], movable);
				options.SetNumber(kMoveOption[a], movable ? float(at[a]) : 0.0F);
				options.SetEnabled(kTurnOption[a], movable);
			}
		}

		void TransformTool::OnOptionToggled(IVoxelEditContext& ed, const std::string& id, bool value) {
			GizmoTool::OnOptionToggled(ed, id, value);
			// The pair acts as radio buttons: a click picks its choice, whatever
			// the toggle it landed on was showing.
			if (id == kAboutSelectionOption)
				ed.SetTurnsAboutModelPivot(false);
			else if (id == kAboutPivotOption)
				ed.SetTurnsAboutModelPivot(true);
		}

		void TransformTool::OnOptionNumberChanged(IVoxelEditContext& ed, const std::string& id,
		                                          float value, bool committed) {
			// A move is journaled as it happens, and there is no preview of it to
			// show, so a box does nothing until the value in it is settled: half
			// a number typed would otherwise be a move made.
			if (!committed)
				return;
			const int axis = AxisOf(kMoveOption, id);
			IntVector3 centre;
			if (axis < 0 || !ed.TransformPivot(centre))
				return;
			const int at[3] = {centre.x, centre.y, centre.z};
			int shift[3] = {0, 0, 0};
			shift[axis] = int(std::lround(value)) - at[axis];
			if (shift[axis] == 0)
				return; // already there
			PlacementTransform t;
			t.shift = MakeIntVector3(shift[0], shift[1], shift[2]);
			ed.TransformPlacement(t);
		}

		void TransformTool::OnAction(IVoxelEditContext& ed, const std::string& id) {
			const int axis = AxisOf(kTurnOption, id);
			if (axis < 0)
				return;
			PlacementTransform t;
			t.axis = axis;
			t.quarterTurns = 1;
			ed.TransformPlacement(t);
		}
	} // namespace gui
} // namespace spades
