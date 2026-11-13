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

#include "ModifyTool.h"

#include "SelectTool.h"
#include "VoxelEditContext.h"

namespace spades {
	namespace gui {
		namespace {
			const char* const kFillOption = "modify.fill";
			const char* const kCavityOption = "modify.cavity";
			// A wall one voxel thick. The dig takes a thickness and the box to
			// set it can go on the bar the day one is wanted; until then the
			// thinnest wall is the one a model this size is hollowed to.
			constexpr int kCavityThickness = 1;
			// Transform's place among the sub-tools.
			constexpr int kTransform = 0;
		} // namespace

		ModifyTool::ModifyTool() {
			AddSub(std::unique_ptr<VoxelTool>(new TransformSubTool())); // kTransform
			AddSub(std::unique_ptr<VoxelTool>(new SweepSubTool(false)));
			AddSub(std::unique_ptr<VoxelTool>(new SweepSubTool(true)));

			// Its commands stand on the strip while it is only on offer.
			options.AddAction(kFillOption, "Fill");
			options.SetStanding(kFillOption);
			options.AddAction(kCavityOption, "Cavity");
			options.SetStanding(kCavityOption);
		}

		void ModifyTool::TakePlacement(IVoxelEditContext& ed) {
			if (ed.HasPlacement() && active != kTransform)
				SetSubTool(ed, kTransform);
		}

		void ModifyTool::OnActivate(IVoxelEditContext& ed) {
			// Before the sub-tool it was left on is activated for nothing.
			if (ed.HasPlacement())
				active = kTransform;
			ContainerTool::OnActivate(ed);
		}

		void ModifyTool::OnDocumentChanged(IVoxelEditContext& ed) {
			ContainerTool::OnDocumentChanged(ed);
			TakePlacement(ed);
		}

		void ModifyTool::UpdateOptions(IVoxelEditContext& ed) {
			ContainerTool::UpdateOptions(ed);
			// Whether the selection is on the model's outside is the dig's own
			// answer, which costs a walk of the volume: it is asked when the
			// button is pressed, and refused there with the reason. Voxels
			// still waiting to be placed are not a selection to hollow out,
			// though the count of what the commands act on includes them.
			options.SetEnabled(kFillOption, ed.SelectionCount() > 0);
			options.SetEnabled(kCavityOption, ed.SelectionCount() > 0 && !ed.HasPlacement());
		}

		void ModifyTool::OnAction(IVoxelEditContext& ed, const std::string& id) {
			ContainerTool::OnAction(ed, id);
			if (id == kFillOption)
				ed.RecolorSelection(ed.CurrentColor()); // reports what it recoloured
			else if (id == kCavityOption)
				ed.CarveCavity(kCavityThickness); // reports what it took, or why not
		}

		void ModifyTool::OnBrushColorChanged(IVoxelEditContext& ed, std::uint32_t color,
		                                     bool committed) {
			RecolourSelectionAsChosen(ed, color, committed);
		}
	} // namespace gui
} // namespace spades
