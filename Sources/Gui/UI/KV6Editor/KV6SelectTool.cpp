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

#include "KV6SelectTool.h"
#include "KV6EditorContext.h"
#include "KV6SubToolRegistry.h"

#include <Core/VoxelModel.h>

namespace spades {
	namespace gui {
		namespace {
			const char* kSelectAllOption = "select.all";
			const char* kClearSelectionOption = "select.clear";
		} // namespace

		SelectTool::SelectTool() {
			// Rect adds its solid cells to the selection (LMB) or removes them (RMB).
			auto select = [](IEditorContext& ed, const std::vector<IntVector3>& cells) {
				ed.SelectCells(cells);
			};
			auto deselect = [](IEditorContext& ed, const std::vector<IntVector3>& cells) {
				ed.DeselectCells(cells);
			};
			subs.push_back(std::unique_ptr<EditorTool>(new PointSubTool()));
			subs.push_back(std::unique_ptr<EditorTool>(new RectSubTool("Rect", select, deselect)));
			subs.push_back(std::unique_ptr<EditorTool>(new ByColourSubTool()));
			subs.push_back(std::unique_ptr<EditorTool>(new TransformSubTool()));

			// Sub-tools contributed by scripts (e.g. the Cylinder), appended after
			// the built-in ones.
			SubToolRegistry::Instance().BuildFor(SubToolTarget::Select, subs);

			// Whole-selection commands, available whichever sub-tool is active.
			options.AddAction(kSelectAllOption, "Select All");
			options.AddAction(kClearSelectionOption, "Select None");
		}

		ToolOptions* SelectTool::Options() { return &options; }

		void SelectTool::SelectAll(IEditorContext& ed) {
			VoxelModel& model = ed.Model();
			ed.SelectBox(MakeIntVector3(0, 0, 0), MakeIntVector3(model.GetWidth() - 1,
			                                                     model.GetHeight() - 1,
			                                                     model.GetDepth() - 1));
		}

		void SelectTool::OnAction(IEditorContext& ed, const std::string& id) {
			// Both report a count: selecting is invisible on a model that was
			// already fully selected, and a button that seems to do nothing reads
			// as broken. Matches SelectLinkedColor / Copy / Cut.
			if (id == kSelectAllOption) {
				SelectAll(ed);
				ed.SetStatus("Selected " + std::to_string(ed.SelectionCount()) + " voxels");
			} else if (id == kClearSelectionOption) {
				int count = ed.SelectionCount();
				ed.ClearSelection();
				ed.SetStatus(count ? "Deselected " + std::to_string(count) + " voxels"
				                   : "Nothing was selected");
			}
		}

		bool SelectTool::OnEscape(IEditorContext& ed) {
			// A pending rect or move owns Escape first; only once nothing is in
			// progress does it fall through to dropping the selection. With no
			// selection either, it belongs to the pause menu.
			if (ContainerTool::OnEscape(ed))
				return true;
			if (ed.SelectionCount() == 0)
				return false;
			ed.ClearSelection();
			ed.SetStatus("Selection cleared");
			return true;
		}
	} // namespace gui
} // namespace spades
