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

#include "KV6MirrorTool.h"
#include "KV6EditorContext.h"
#include "KV6SubTool.h"

namespace spades {
	namespace gui {
		namespace {
			const char* const kAxisOption[3] = {"mirror.x", "mirror.y", "mirror.z"};
			const char* const kPlaneOption[3] = {"mirror.plane.x", "mirror.plane.y",
			                                     "mirror.plane.z"};
			const char* const kAxisLabel[3] = {"X", "Y", "Z"};
			const char* const kResetOption = "mirror.reset";
		} // namespace

		void AddMirrorToggles(ToolOptions& options) {
			options.AddBool(kAxisOption[0], "X", "Mirror");
			options.AddBool(kAxisOption[1], "Y", "Mirror");
			options.AddBool(kAxisOption[2], "Z", "Mirror");
		}

		void SyncMirrorToggles(ToolOptions& options, IEditorContext& ed) {
			for (int a = 0; a < 3; a++)
				options.SetBool(kAxisOption[a], ed.MirrorEnabled(a));
		}

		bool ApplyMirrorToggle(IEditorContext& ed, const std::string& id, bool value) {
			for (int a = 0; a < 3; a++) {
				if (id == kAxisOption[a]) {
					ed.SetMirrorEnabled(a, value);
					return true;
				}
			}
			return false;
		}

		// A reflection only shifts at half a voxel, so the planes always sit on
		// that grid (PlaceMirrorPlane holds them to it) and 0.1 is not offered.
		MirrorTool::MirrorTool()
		    : GizmoTool(std::unique_ptr<GizmoSubTool>(new MirrorGizmoSubTool()), {0.5F, 1.0F},
		                0.5F) {
			AddMirrorToggles(options);
			// A box per axis, which both shows where a plane is and is how it is
			// moved. They step by half a voxel because that is the grid the
			// planes sit on; a whole step from a half keeps the half.
			for (int a = 0; a < 3; a++)
				options.AddNumber(kPlaneOption[a], kAxisLabel[a], "Plane", kPlaneStep, kDecimals);
			options.AddAction(kResetOption, "Reset to Pivot");
			AddSnapOptions();
		}

		int MirrorTool::AxisOf(const std::string& id) {
			for (int a = 0; a < 3; a++)
				if (id == kPlaneOption[a])
					return a;
			return -1;
		}

		void MirrorTool::UpdateOptions(IEditorContext& ed) {
			GizmoTool::UpdateOptions(ed);
			SyncMirrorToggles(options, ed);
			// Whatever moved a plane — a gizmo drag, Reset, undo, a box — the
			// boxes show where they are. The bar leaves the one being typed
			// into alone.
			const Vector3 plane = ed.MirrorPlane();
			const float at[3] = {plane.x, plane.y, plane.z};
			for (int a = 0; a < 3; a++)
				options.SetNumber(kPlaneOption[a], at[a]);
		}

		void MirrorTool::OnOptionNumberChanged(IEditorContext& ed, const std::string& id,
		                                       float value, bool committed) {
			const int a = AxisOf(id);
			if (a < 0)
				return;
			// The other two axes come from the planes as they stand, which are
			// the previewed ones while a box is being typed into: typing X then
			// Y keeps both, rather than the second putting the first back.
			Vector3 plane = ed.MirrorPlane();
			(a == 0 ? plane.x : a == 1 ? plane.y : plane.z) = value;
			// The editor quantises to the half-voxel grid either way, so a value
			// between the steps lands on the nearer one rather than being refused.
			if (committed)
				ed.SetMirrorPlane(plane);
			else
				ed.PreviewMirrorPlane(plane);
		}

		void MirrorTool::OnOptionToggled(IEditorContext& ed, const std::string& id, bool value) {
			GizmoTool::OnOptionToggled(ed, id, value);
			ApplyMirrorToggle(ed, id, value);
		}

		void MirrorTool::OnAction(IEditorContext& ed, const std::string& id) {
			if (id == kResetOption) {
				ed.ResetMirrorPlane();
				ed.SetStatus("Mirror planes reset to the pivot");
			}
		}
	} // namespace gui
} // namespace spades
