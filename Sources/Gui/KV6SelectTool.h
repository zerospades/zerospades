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

#pragma once

#include <Core/Math.h>

#include "KV6EditorTool.h"

namespace spades {
	namespace gui {
		// Select voxels, via sub-tools shown in the secondary toolbar:
		//  - Point:    click toggles the voxel under the cursor.
		//  - Rect:     3 clicks (corner, opposite corner, depth) select a box.
		//  - By Colour: click flood-fills 6-connected voxels of the same colour.
		// [L] also flood-fills from the cursor. Clicking empty space clears.
		class SelectTool : public EditorTool {
		public:
			const char* Label() const override { return "Select"; }
			void OnActivate(KV6EditorView&) override;
			void OnPointerDown(KV6EditorView&, const std::string& button) override;
			void OnKey(KV6EditorView&, const std::string& key, bool down) override;
			void DrawScene(KV6EditorView&) override;

			int SubToolCount() const override { return SubCount; }
			const char* SubToolLabel(int i) const override;
			int SubTool() const override { return sub; }
			void SetSubTool(int i) override;

		private:
			enum Sub { Point, Rect, ByColour, SubCount };
			int sub = Point;

			// 3-point rectangle box state.
			int stage = 0;            // 0 none, 1 have corner, 2 have rectangle
			IntVector3 p1;            // first corner
			int normalAxis = 2;       // axis of the clicked face's normal
			IntVector3 rectLo, rectHi; // in-plane rectangle (after the 2nd click)

			void ResetRect();
			// The provisional box for the current rect stage given hover voxel `cur`.
			void RectBox(const IntVector3& cur, IntVector3& lo, IntVector3& hi) const;
		};
	} // namespace gui
} // namespace spades
