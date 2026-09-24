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

#include "KV6TransformTool.h"
#include "KV6EditorContext.h"

#include <cmath>

namespace spades {
	namespace gui {
		namespace {
			const char* const kPlaceOption = "transform.place";
			const char* const kCancelOption = "transform.cancel";
			const char* const kAboutSelectionOption = "transform.about.selection";
			const char* const kAboutPivotOption = "transform.about.pivot";
			const char* const kMoveOption[3] = {"transform.move.x", "transform.move.y",
			                                    "transform.move.z"};
			const char* const kTurnOption[3] = {"transform.turn.x", "transform.turn.y",
			                                    "transform.turn.z"};
			const char* const kAxisLabel[3] = {"X", "Y", "Z"};
			// Voxels land on voxels, so a turn is a right angle or nothing; the
			// boxes are in degrees because that is what a turn is asked for in.
			const float kDegreesPerQuarterTurn = 90.0F;

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
			options.AddAction(kPlaceOption, "Place");
			options.AddAction(kCancelOption, "Cancel");
			// Whole voxels and right angles: neither box has anything to say
			// after the decimal point.
			for (int a = 0; a < 3; a++)
				options.AddNumber(kMoveOption[a], kAxisLabel[a], "Move", 1.0F, 0);
			for (int a = 0; a < 3; a++)
				options.AddNumber(kTurnOption[a], kAxisLabel[a], "Turn", kDegreesPerQuarterTurn, 0);
			options.AddBool(kAboutSelectionOption, "Selection", "Turn about");
			options.AddBool(kAboutPivotOption, "Pivot", "Turn about");
			AddSnapOptions();
		}

		void TransformTool::UpdateOptions(IEditorContext& ed) {
			GizmoTool::UpdateOptions(ed);
			// Place and Cancel act on pending voxels; a selection that has not
			// moved yet is not pending, so there is nothing for them to do.
			const bool pending = ed.HasPlacement();
			options.SetEnabled(kPlaceOption, pending);
			options.SetEnabled(kCancelOption, pending);

			// The turn centre is the editor's setting; the pair only shows it.
			const bool aboutPivot = ed.TurnsAboutModelPivot();
			options.SetBool(kAboutSelectionOption, !aboutPivot);
			options.SetBool(kAboutPivotOption, aboutPivot);

			// The Move boxes are where the voxels are, so they follow a gizmo
			// drag, a paste and undo alike. With nothing to move there is no
			// centre to show, and both rows grey out beside Place and Cancel.
			IntVector3 centre;
			const bool movable = ed.TransformPivot(centre);
			const int at[3] = {centre.x, centre.y, centre.z};
			// How far the pending voxels have been turned already. With none
			// pending there is nothing that has been turned, so the boxes read
			// zero: selecting, deselecting, placing or dropping all end the
			// placement, and with it the record.
			IntVector3 turns;
			if (!ed.TransformTurns(turns))
				turns = MakeIntVector3(0, 0, 0);
			const int quarters[3] = {turns.x, turns.y, turns.z};

			for (int a = 0; a < 3; a++) {
				options.SetEnabled(kMoveOption[a], movable);
				options.SetNumber(kMoveOption[a], movable ? float(at[a]) : 0.0F);
				options.SetEnabled(kTurnOption[a], movable);
				options.SetNumber(kTurnOption[a],
				                  movable ? float(quarters[a]) * kDegreesPerQuarterTurn : 0.0F);
			}
		}

		void TransformTool::OnOptionToggled(IEditorContext& ed, const std::string& id, bool value) {
			GizmoTool::OnOptionToggled(ed, id, value);
			// The pair acts as radio buttons: a click picks its choice, whatever
			// the toggle it landed on was showing.
			if (id == kAboutSelectionOption)
				ed.SetTurnsAboutModelPivot(false);
			else if (id == kAboutPivotOption)
				ed.SetTurnsAboutModelPivot(true);
		}

		void TransformTool::OnOptionNumberChanged(IEditorContext& ed, const std::string& id,
		                                          float value, bool committed) {
			// Moving and turning voxels is journaled as it happens, and there is
			// no preview of it to show, so a box does nothing until the value in
			// it is settled: half a number typed would otherwise be a move made.
			if (!committed)
				return;
			IntVector3 centre;
			if (!ed.TransformPivot(centre))
				return;

			int axis = AxisOf(kMoveOption, id);
			if (axis >= 0) {
				const int at[3] = {centre.x, centre.y, centre.z};
				int shift[3] = {0, 0, 0};
				shift[axis] = int(std::lround(value)) - at[axis];
				if (shift[axis] == 0)
					return; // already there
				PlacementTransform t;
				t.shift = MakeIntVector3(shift[0], shift[1], shift[2]);
				ed.TransformPlacement(t);
				return;
			}

			axis = AxisOf(kTurnOption, id);
			if (axis >= 0) {
				// The box holds how far these voxels have been turned already, so
				// what is typed or stepped is where it should get to: the turn to
				// make is the difference. Anything between right angles is taken
				// as the nearest one, since a voxel turned by less would not land
				// on a voxel.
				IntVector3 turns;
				if (!ed.TransformTurns(turns))
					turns = MakeIntVector3(0, 0, 0); // nothing lifted yet: from zero
				const int quarters[3] = {turns.x, turns.y, turns.z};
				PlacementTransform t;
				t.axis = axis;
				t.quarterTurns =
				  int(std::lround(value / kDegreesPerQuarterTurn)) - quarters[axis];
				if (!t.Turns())
					return;
				ed.TransformPlacement(t);
			}
		}

		void TransformTool::OnAction(IEditorContext& ed, const std::string& id) {
			if (id == kPlaceOption)
				ed.ApplyPlacement();
			else if (id == kCancelOption)
				ed.CancelPlacement();
		}
	} // namespace gui
} // namespace spades
