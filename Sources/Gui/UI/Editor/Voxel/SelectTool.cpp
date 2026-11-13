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
			const char* const kSelectAllOption = "select.all";
			const char* const kSelectNoneOption = "select.none";
			const char* const kInvertOption = "select.invert";
			// The bar's two bands: voxels chosen in the model, and voxels chosen
			// on the screen.
			const char* const kInModelGroup = "model";
			const char* const kOnScreenGroup = "screen";
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

			// The commands that change the selection whichever way of choosing
			// is active. The Select menu has them too, with their keys.
			options.AddAction(kSelectAllOption, "All");
			options.AddAction(kSelectNoneOption, "None");
			options.AddAction(kInvertOption, "Invert");
		}

		void SelectTool::OnPointer(IVoxelEditContext& ed, const PointerInput& e) {
			if (e.IsDown() && e.IsLeft()) {
				// Asked before the sub-tool sees the press: a box under way says so.
				const bool placing = Cur() && !Cur()->EscapeLabel(ed).empty();
				ed.DoPick();
				pressOnNothing = !placing && !ed.HasPick();
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
			options.SetEnabled(kSelectNoneOption, ed.SelectionCount() > 0);
		}

		void RecolourSelectionAsChosen(IVoxelSelection& ed, std::uint32_t color, bool committed) {
			if (ed.SelectionCount() == 0)
				return;
			if (committed)
				ed.RecolorSelection(color);
			else
				ed.PreviewRecolorSelection(color);
		}

		void SelectTool::OnBrushColorChanged(IVoxelEditContext& ed, std::uint32_t color,
		                                     bool committed) {
			RecolourSelectionAsChosen(ed, color, committed);
		}

		// Each reports what it did, as it does from the menu and from its key.
		void SelectTool::OnAction(IVoxelEditContext& ed, const std::string& id) {
			ContainerTool::OnAction(ed, id);
			if (id == kSelectAllOption)
				ed.SelectAll();
			else if (id == kSelectNoneOption)
				ed.ClearSelection();
			else if (id == kInvertOption)
				ed.InvertSelection();
		}
	} // namespace gui
} // namespace spades
