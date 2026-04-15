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
		void DrawTool::OnPointerDown(KV6EditorView& ed, const std::string& button) {
			if (button == "LeftMouseButton") {
				if (ed.AltHeld() || ed.PickModeActive()) {
					ed.Eyedropper();
					ed.ClearPickMode();
					return;
				}
				ed.PlaceCube();
			} else if (button == "RightMouseButton") {
				ed.DeleteCube();
			}
		}

		void DrawTool::DrawScene(KV6EditorView& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 p = ed.PickPlace();
			IntVector3 h = ed.PickSolid();
			// Where a new voxel would land (current colour) and the targeted voxel.
			ed.DrawCellOutline(p.x, p.y, p.z, ed.ColorToVec(ed.CurrentColor()));
			ed.DrawCellOutline(h.x, h.y, h.z, MakeVector4(1.0F, 0.9F, 0.3F, 0.9F));
		}
	} // namespace gui
} // namespace spades
