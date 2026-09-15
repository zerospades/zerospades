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

#include "KV6PaintTool.h"
#include "KV6EditorContext.h"
#include "KV6SubToolRegistry.h"

namespace spades {
	namespace gui {
		PaintTool::PaintTool() {
			// Rect recolours every existing voxel it spans with the current colour.
			// Recolouring has no inverse, so both mouse buttons do the same thing.
			auto paint = [](IEditorContext& ed, const std::vector<IntVector3>& cells) {
				ed.PaintCells(cells, ed.CurrentColor());
			};
			subs.push_back(std::unique_ptr<EditorTool>(new PaintBlockSubTool()));
			subs.push_back(std::unique_ptr<EditorTool>(
			  new RectSubTool("Rect", paint, paint, true, "Painted", "Painted")));

			// Mirror each recolour across the pivot plane, then the brush swatch.
			options.AddBool("mirror.x", "X", "Mirror");
			options.AddBool("mirror.y", "Y", "Mirror");
			options.AddBool("mirror.z", "Z", "Mirror");
			options.AddColor("color");

			// Sub-tools contributed by scripts (targeting Paint), appended after the
			// built-in ones.
			SubToolRegistry::Instance().BuildFor(SubToolTarget::Paint, subs);
		}
	} // namespace gui
} // namespace spades
