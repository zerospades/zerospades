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

#include "SelectTool.h"

#include <algorithm>
#include "VoxelEditContext.h"

namespace spades {
	namespace gui {
		namespace {
			const char* const kFillSelectionOption = "select.fill";
			const char* const kCavityOption = "select.cavity";
			// A wall one voxel thick. The dig takes a thickness and the box to
			// set it can go on the bar the day one is wanted; until then the
			// thinnest wall is the one a model this size is hollowed to.
			constexpr int kCavityThickness = 1;
			// The bar's three bands: where the voxels are chosen, how they are
			// chosen on the screen, and what is then done to them.
			const char* const kInModelGroup = "model";
			const char* const kOnScreenGroup = "screen";
			const char* const kActOnGroup = "act";
			// Pixels a press may move and still be a click.
			constexpr float kClickReach = 4.0F;
		} // namespace

		SelectTool::SelectTool() {
			// Box and Cylinder add their solid cells to the selection (LMB) or
			// remove them (RMB).
			auto select = [](IVoxelEditContext& ed, const std::vector<IntVector3>& cells) {
				ed.SelectCells(cells);
			};
			auto deselect = [](IVoxelEditContext& ed, const std::vector<IntVector3>& cells) {
				ed.DeselectCells(cells);
			};
			// Ways of choosing voxels: in the model, then on the screen.
			AddSub(std::unique_ptr<VoxelTool>(new SelectVoxelSubTool()), kInModelGroup);
			AddSub(std::unique_ptr<VoxelTool>(
			         new BoxSubTool({select, "select"}, {deselect, "deselect"}, false)),
			       kInModelGroup);
			AddSub(std::unique_ptr<VoxelTool>(new ByColourSubTool()), kInModelGroup);
			AddSub(std::unique_ptr<VoxelTool>(
			         new CylinderSubTool({select, "select"}, {deselect, "deselect"})),
			       kInModelGroup);
			AddSub(std::unique_ptr<VoxelTool>(new PlaneSelectSubTool()), kInModelGroup);
			AddSub(std::unique_ptr<VoxelTool>(new RectSelectSubTool(voxels, screen)),
			       kOnScreenGroup);
			AddSub(std::unique_ptr<VoxelTool>(new BrushSelectSubTool(voxels, screen)),
			       kOnScreenGroup);

			// Then what is done with the voxels chosen. Each needs a selection to
			// work on, so each lives here rather than as a tool of its own: the
			// bar reads left to right as choose, then do.
			actingIndex = int(subs.size());
			AddSub(std::unique_ptr<VoxelTool>(new TransformSubTool()), kActOnGroup);
			AddSub(std::unique_ptr<VoxelTool>(new SweepSubTool(false)), kActOnGroup);
			AddSub(std::unique_ptr<VoxelTool>(new SweepSubTool(true)), kActOnGroup);

			// Only the commands with no key of their own: Select All, Select
			// None, Delete, Copy, Cut and Paste are in the menu with theirs,
			// where a command belongs, leaving this bar to the active sub-tool.
			options.AddAction(kFillSelectionOption, "Fill");
			options.AddAction(kCavityOption, "Cavity");
		}

		void SelectTool::SetSubTool(IVoxelEditContext& ed, int i) {
			ContainerTool::SetSubTool(ed, i);
			if (Selecting())
				lastSelecting = active;
		}

		void SelectTool::OnReturnToSelect(IVoxelEditContext& ed) {
			SetSubTool(ed, lastSelecting);
		}

		void SelectTool::OnPointer(IVoxelEditContext& ed, const PointerInput& e) {
			if (e.IsDown() && e.IsLeft()) {
				// Asked before the sub-tool sees the press: a box under way says so.
				const bool placing = Cur() && !Cur()->EscapeLabel(ed).empty();
				ed.DoPick();
				// Only while choosing voxels. A sub-tool that acts on them has
				// its own answer to a click on nothing -- it hands the selection
				// back to the last way of choosing -- and must not have the
				// selection it is working on taken away underneath it.
				pressOnNothing = Selecting() && !placing && !ed.HasPick();
				pressAt = e.pos;
			}
			if (e.IsDrag() && pressOnNothing &&
			    (e.pos - pressAt).GetManhattanLength() > kClickReach)
				pressOnNothing = false; // a drag, not a click
			ContainerTool::OnPointer(ed, e);
			if (e.IsUp() && e.IsLeft() && pressOnNothing) {
				pressOnNothing = false;
				if (ed.SelectionCount() > 0) {
					ed.ClearSelection();
					ed.SetStatus("Selection cleared");
				}
			}
		}

		void SelectTool::UpdateOptions(IVoxelEditContext& ed) {
			ContainerTool::UpdateOptions(ed);
			const bool selected = ed.SelectionCount() > 0;
			options.SetEnabled(kFillSelectionOption, selected);
			// Whether the selection is on the model's outside is the dig's own
			// answer, which costs a walk of the volume: it is asked when the
			// button is pressed, and refused there with the reason.
			options.SetEnabled(kCavityOption, selected);
		}

		void SelectTool::OnAction(IVoxelEditContext& ed, const std::string& id) {
			ContainerTool::OnAction(ed, id);
			if (id == kFillSelectionOption)
				ed.RecolorSelection(ed.CurrentColor()); // reports what it recoloured
			else if (id == kCavityOption)
				ed.CarveCavity(kCavityThickness); // reports what it took, or why not
		}

		void SelectTool::OnBrushColorChanged(IVoxelEditContext& ed, std::uint32_t color,
		                                     bool committed) {
			if (ed.SelectionCount() == 0)
				return;
			if (committed)
				ed.RecolorSelection(color);
			else
				ed.PreviewRecolorSelection(color);
		}

		std::string SelectTool::Hint(IVoxelEditContext& ed) {
			std::string hint = ContainerTool::Hint(ed);
			if (ed.SelectionCount() > 0)
				hint += std::string(hint.empty() ? "" : "  |  ") + "colour picker recolours the selection";
			return hint;
		}
	} // namespace gui
} // namespace spades
