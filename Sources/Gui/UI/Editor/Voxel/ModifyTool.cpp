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

#include "VoxelEditContext.h"

namespace spades {
	namespace gui {
		namespace {
			const char* const kCavityOption = "modify.cavity";
			// A wall one voxel thick. The dig takes a thickness and the box to
			// set it can go on the bar the day one is wanted; until then the
			// thinnest wall is the one a model this size is hollowed to.
			constexpr int kCavityThickness = 1;
			// The strip's two bands: what changes where the voxels are, and what
			// changes their colour.
			const char* const kShapeGroup = "shape";
			const char* const kColourGroup = "colour";
		} // namespace

		ModifyTool::ModifyTool() {
			AddSub(std::unique_ptr<VoxelTool>(new TransformSubTool()), kShapeGroup);
			AddSub(std::unique_ptr<VoxelTool>(new SweepSubTool(false)), kShapeGroup);
			AddSub(std::unique_ptr<VoxelTool>(new SweepSubTool(true)), kShapeGroup);
			AddSub(std::unique_ptr<VoxelTool>(new PaintSelectionSubTool()), kColourGroup);

			commands.AddAction(kCavityOption, "Cavity");
		}

		bool ModifyTool::Available(const IVoxelEditContext& ed) const {
			return ed.SelectionCount() > 0; // a waiting placement counts
		}

		void ModifyTool::UpdateOptions(IVoxelEditContext& ed) {
			ContainerTool::UpdateOptions(ed);
			// Whether the selection is on the model's outside is the dig's own
			// answer, which costs a walk of the volume: it is asked when the
			// button is pressed, and refused there with the reason. Voxels
			// still waiting to be placed are not a selection to hollow out,
			// though the count of what the commands act on includes them.
			commands.SetEnabled(kCavityOption, ed.SelectionCount() > 0 && !ed.HasPlacement());
		}

		void ModifyTool::OnAction(IVoxelEditContext& ed, const std::string& id) {
			ContainerTool::OnAction(ed, id);
			if (id == kCavityOption)
				ed.CarveCavity(kCavityThickness); // reports what it took, or why not
		}

		void ModifyTool::OnBrushColorChanged(IVoxelEditContext& ed, std::uint32_t color,
		                                     bool committed) {
			if (VoxelTool* sub = Cur())
				sub->OnBrushColorChanged(ed, color, committed);
		}
	} // namespace gui
} // namespace spades
