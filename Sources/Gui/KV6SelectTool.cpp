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
#include "KV6EditorView.h"

namespace spades {
	namespace gui {
		void SelectTool::OnActivate(KV6EditorView& ed) {
			ed.SetStatus("Select: click voxels to (de)select, click empty to clear");
		}

		void SelectTool::OnPointerDown(KV6EditorView& ed, const std::string& button) {
			if (button != "LeftMouseButton")
				return;
			ed.DoPick();
			if (ed.HasPick()) {
				IntVector3 h = ed.PickSolid();
				ed.ToggleSelect(h.x, h.y, h.z);
			} else {
				ed.ClearSelection();
			}
		}

		void SelectTool::DrawScene(KV6EditorView& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 h = ed.PickSolid();
			// Hovered voxel: red if already selected (would deselect), else cyan.
			Vector4 col = ed.IsSelected(h.x, h.y, h.z) ? MakeVector4(1.0F, 0.3F, 0.3F, 0.95F)
			                                           : MakeVector4(0.3F, 0.8F, 1.0F, 0.95F);
			ed.DrawCellOutline(h.x, h.y, h.z, col);
		}
	} // namespace gui
} // namespace spades
