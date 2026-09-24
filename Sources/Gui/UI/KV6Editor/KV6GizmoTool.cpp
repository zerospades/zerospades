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

#include "KV6GizmoTool.h"

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
			options.AddBool(kGridOption, "Snap to Grid");
		}

		bool GizmoTool::Offers(float candidate) const {
			for (float s : steps) {
				if (SameStep(s, candidate))
					return true;
			}
			return false;
		}

		bool GizmoTool::GridSnapApplies() const { return step > grid && !SameStep(step, grid); }

		void GizmoTool::ApplySnap() { gizmo->SetTranslationSnap(step, toGrid && GridSnapApplies()); }

		void GizmoTool::UpdateOptions(IEditorContext&) {
			// The steps act as radio buttons, so each shows whether it is the one
			// in use, whatever a click on it flipped it to.
			for (const StepChoice& choice : kStepChoices) {
				options.SetBool(choice.id, SameStep(choice.step, step));
				options.SetEnabled(choice.id, Offers(choice.step));
			}
			// Kept while unavailable, so going back to a coarser step restores it.
			options.SetBool(kGridOption, toGrid);
			options.SetEnabled(kGridOption, GridSnapApplies());
		}

		void GizmoTool::OnOptionToggled(IEditorContext&, const std::string& id, bool value) {
			if (id == kGridOption) {
				toGrid = value;
				ApplySnap();
				return;
			}
			for (const StepChoice& choice : kStepChoices) {
				if (id == choice.id && Offers(choice.step)) {
					step = choice.step;
					ApplySnap();
					return;
				}
			}
		}
	} // namespace gui
} // namespace spades
