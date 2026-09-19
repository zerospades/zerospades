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

#include <cstdio>

namespace spades {
	namespace gui {
		namespace {
			const char* const kPlaceOption = "transform.place";
			const char* const kCancelOption = "transform.cancel";
			const char* const kAboutSelectionOption = "transform.about.selection";
			const char* const kAboutPivotOption = "transform.about.pivot";
			const char* const kReadoutOption = "transform.readout";
		} // namespace

		// Voxels only ever land on voxels: they move by whole ones, and always
		// sit on the grid, so neither a finer step nor Snap to Grid applies.
		TransformTool::TransformTool()
		    : GizmoTool(std::unique_ptr<GizmoSubTool>(new TransformSubTool()), {1.0F}, false) {
			options.AddAction(kPlaceOption, "Place");
			options.AddAction(kCancelOption, "Cancel");
			options.AddBool(kAboutSelectionOption, "Selection", "Turn about");
			options.AddBool(kAboutPivotOption, "Pivot", "Turn about");
			AddSnapOptions();
			options.AddLabel(kReadoutOption);
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

			IntVector3 centre;
			if (ed.TransformPivot(centre)) {
				char buf[80];
				std::snprintf(buf, sizeof(buf), "Centre  %d, %d, %d", centre.x, centre.y,
				              centre.z);
				options.SetLabel(kReadoutOption, buf);
			} else {
				options.SetLabel(kReadoutOption, "Nothing to move");
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

		void TransformTool::OnAction(IEditorContext& ed, const std::string& id) {
			if (id == kPlaceOption)
				ed.ApplyPlacement();
			else if (id == kCancelOption)
				ed.CancelPlacement();
		}
	} // namespace gui
} // namespace spades
