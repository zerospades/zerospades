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
			// Rect fills its cells with the current colour (LMB) or erases them (RMB).
			auto fill = [](KV6EditorView& ed, const std::vector<IntVector3>& cells) {
				ed.FillCells(cells, ed.CurrentColor());
			};
			auto erase = [](KV6EditorView& ed, const std::vector<IntVector3>& cells) {
				ed.EraseCells(cells);
			};
			subs.push_back(std::unique_ptr<EditorTool>(new BlockSubTool()));
			subs.push_back(std::unique_ptr<EditorTool>(new RectSubTool("Rect", fill, erase, true)));
		}
	} // namespace gui
} // namespace spades
