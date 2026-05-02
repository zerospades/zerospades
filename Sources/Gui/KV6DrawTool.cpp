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

#include "KV6DrawTool.h"
#include "KV6EditorView.h"

namespace spades {
	namespace gui {
		DrawTool::DrawTool() {
			// Region shapes fill their cells with the current colour.
			auto fill = [](KV6EditorView& ed, const std::vector<IntVector3>& cells) {
				ed.FillCells(cells, ed.CurrentColor());
			};
			subs.push_back(std::unique_ptr<SubTool>(new BlockSubTool()));
			subs.push_back(std::unique_ptr<SubTool>(new ShapeSubTool(ShapeSubTool::Rect, "Rect", fill)));
			subs.push_back(
			  std::unique_ptr<SubTool>(new ShapeSubTool(ShapeSubTool::Cylinder, "Cylinder", fill)));
		}
	} // namespace gui
} // namespace spades
