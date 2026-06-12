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

namespace spades {
	namespace gui {
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
			subs.push_back(std::unique_ptr<EditorTool>(new MoveSubTool()));
		}
	} // namespace gui
} // namespace spades
