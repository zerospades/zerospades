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
			const char* const kClearSelectionOption = "select.clear";
			const char* const kDeleteSelectionOption = "select.delete";
			const char* const kFillSelectionOption = "select.fill";
			const char* const kCopyOption = "clipboard.copy";
			const char* const kCutOption = "clipboard.cut";
			const char* const kPasteOption = "clipboard.paste";
			const char* const kDiameterOption = "select.diameter";
			const char* const kXRayOption = "select.xray";
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
			subs.push_back(std::unique_ptr<VoxelTool>(new SelectVoxelSubTool()));
			subs.push_back(std::unique_ptr<VoxelTool>(
			  new BoxSubTool({select, "select"}, {deselect, "deselect"}, false)));
			subs.push_back(std::unique_ptr<VoxelTool>(new ByColourSubTool()));
			subs.push_back(std::unique_ptr<VoxelTool>(
			  new CylinderSubTool({select, "select"}, {deselect, "deselect"})));
			subs.push_back(std::unique_ptr<VoxelTool>(new PlaneSelectSubTool()));
			rectIndex = int(subs.size());
			subs.push_back(std::unique_ptr<VoxelTool>(new RectSelectSubTool(voxels, screen)));
			brushIndex = int(subs.size());
			subs.push_back(std::unique_ptr<VoxelTool>(new BrushSelectSubTool(voxels, screen)));

			// Whole-selection commands, available whichever sub-tool is active.
			// Their keys work in every tool; these buttons are their one home.
			options.AddAction(kSelectAllOption, "Select All");
			options.AddAction(kClearSelectionOption, "Select None");
			options.AddAction(kDeleteSelectionOption, "Delete");
			options.AddAction(kFillSelectionOption, "Fill");
			options.AddAction(kCopyOption, "Copy", "Clipboard");
			options.AddAction(kCutOption, "Cut", "Clipboard");
			options.AddAction(kPasteOption, "Paste", "Clipboard");
			// The screen selections' settings, shown with them only.
			options.AddNumber(kDiameterOption, "Size", "Brush", 5.0F, 0);
			options.AddBool(kXRayOption, "X-Ray", "", screen.through);
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
			const bool selected = ed.SelectionCount() > 0;
			options.SetEnabled(kClearSelectionOption, selected);
			options.SetEnabled(kDeleteSelectionOption, selected);
			options.SetEnabled(kFillSelectionOption, selected);
			options.SetEnabled(kCopyOption, selected);
			options.SetEnabled(kCutOption, selected);
			options.SetEnabled(kPasteOption, ed.CanPaste());
			options.SetVisible(kDiameterOption, active == brushIndex);
			options.SetVisible(kXRayOption, active == rectIndex || active == brushIndex);
			options.SetNumber(kDiameterOption, screen.diameter);
			options.SetBool(kXRayOption, screen.through);
		}

		void SelectTool::OnOptionToggled(IVoxelEditContext& ed, const std::string& id, bool value) {
			ContainerTool::OnOptionToggled(ed, id, value);
			if (id == kXRayOption)
				screen.through = value;
		}

		void SelectTool::OnOptionNumberChanged(IVoxelEditContext& ed, const std::string& id,
		                                       float value, bool committed) {
			ContainerTool::OnOptionNumberChanged(ed, id, value, committed);
			if (id == kDiameterOption)
				screen.diameter = std::max(ScreenSelectSettings::kMinDiameter,
				                           std::min(ScreenSelectSettings::kMaxDiameter, value));
		}

		void SelectTool::OnAction(IVoxelEditContext& ed, const std::string& id) {
			ContainerTool::OnAction(ed, id);
			// Each reports a count: selecting is invisible on a model that was
			// already fully selected, and a button that seems to do nothing reads
			// as broken. Matches By Colour / Copy / Cut.
			if (id == kSelectAllOption) {
				ed.SelectAll();
				ed.SetStatus("Selected " + std::to_string(ed.SelectionCount()) + " voxels");
			} else if (id == kDeleteSelectionOption) {
				ed.DeleteSelection(); // reports what it removed, or why not
			} else if (id == kFillSelectionOption) {
				ed.RecolorSelection(ed.CurrentColor());
			} else if (id == kClearSelectionOption) {
				int count = ed.SelectionCount();
				ed.ClearSelection();
				ed.SetStatus(count ? "Deselected " + std::to_string(count) + " voxels"
				                   : "Nothing was selected");
			} else if (id == kCopyOption) {
				ed.CopySelection();
			} else if (id == kCutOption) {
				ed.CutSelection();
			} else if (id == kPasteOption) {
				ed.Paste();
			}
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
