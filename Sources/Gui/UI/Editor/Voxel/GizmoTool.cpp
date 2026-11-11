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

#include "GizmoTool.h"

#include <cmath>

namespace spades {
	namespace gui {
		namespace {
			struct StepChoice {
				float step;
				const char* id;
				const char* label;
			};
			const StepChoice kStepChoices[] = {{1.0F, "snap.step.1", "1"},
			                                   {0.5F, "snap.step.0.5", "0.5"},
			                                   {0.1F, "snap.step.0.1", "0.1"}};
			const char* const kStepGroup = "Step";
			const char* const kGridOption = "snap.grid";

			bool SameStep(float a, float b) { return std::fabs(a - b) < 1.0e-4F; }
		} // namespace

		GizmoTool::GizmoTool(std::unique_ptr<GizmoSubTool> gizmoTool, std::vector<float> offered,
		                     float grid)
		    : gizmo(gizmoTool.get()), steps(std::move(offered)), grid(grid),
		      step(steps.empty() ? 0.0F : steps.front()) {
			subs.push_back(std::move(gizmoTool));
			ApplySnap();
		}

		void GizmoTool::AddSnapOptions() {
			for (const StepChoice& choice : kStepChoices)
				options.AddBool(choice.id, choice.label, kStepGroup);
			// A one-off, so a button that does something rather than a toggle
			// that latches, and only where there is ever anything to round.
			if (GridSnapApplies())
				options.AddAction(kGridOption, "Snap to Grid");
		}

		bool GizmoTool::Offers(float candidate) const {
			for (float s : steps) {
				if (SameStep(s, candidate))
					return true;
			}
			return false;
		}

		// Snap to Grid rounds what is moved to the whole voxels -- the
		// coordinates the voxels themselves have. It tidies up a position
		// dragged roughly into place; half a voxel along is where a pivot or a
		// mirror plane belongs about as often, and from the grid that is one
		// press of a 0.5 step away.
		Vector3 GizmoTool::OnGrid(const Vector3& v) {
			auto nearest = [](float c) {
				return std::round((c - kGridAt) / kGridStep) * kGridStep + kGridAt;
			};
			return MakeVector3(nearest(v.x), nearest(v.y), nearest(v.z));
		}

		bool GizmoTool::GridSnapApplies() const {
			// Only where what is moved can be off the grid in the first place:
			// voxels never are, as they only ever sit on whole voxels.
			return grid < kGridStep && !SameStep(grid, kGridStep);
		}

		void GizmoTool::ApplySnap() { gizmo->SetTranslationSnap(step); }

		void GizmoTool::UpdateOptions(IVoxelEditContext& ed) {
			ContainerTool::UpdateOptions(ed);
			// The steps act as radio buttons, so each shows whether it is the one
			// in use, whatever a click on it flipped it to.
			for (const StepChoice& choice : kStepChoices) {
				options.SetBool(choice.id, SameStep(choice.step, step));
				options.SetEnabled(choice.id, Offers(choice.step));
			}
		}

		// The steps are radio buttons: a click picks its choice, whatever the
		// toggle it landed on was showing, so the value it was flipped to is of
		// no interest here.
		void GizmoTool::OnOptionToggled(IVoxelEditContext& ed, const std::string& id, bool value) {
			ContainerTool::OnOptionToggled(ed, id, value);
			for (const StepChoice& choice : kStepChoices) {
				if (id == choice.id && Offers(choice.step)) {
					step = choice.step;
					ApplySnap();
					return;
				}
			}
		}

		// Subclasses with actions of their own call this first.
		void GizmoTool::OnAction(IVoxelEditContext& ed, const std::string& id) {
			ContainerTool::OnAction(ed, id);
			if (id == kGridOption)
				SnapToGrid(ed);
		}
	} // namespace gui
} // namespace spades
