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

#include "KV6ContainerTool.h"

namespace spades {
	namespace gui {
		// Recolour existing voxels: Block (single, drag to keep painting) and Rect
		// (recolour a box). The geometry is never changed, only the colours.
		class PaintTool : public ContainerTool {
		public:
			PaintTool();
			const char* Label() const override { return "Paint"; }
			// Cells from sub-tools (incl. scripted ones) recolour rather than fill.
			EditorRole Role() const override { return EditorRole::Paint; }
			ToolOptions* Options() override { return &options; }

		private:
			ToolOptions options; // mirror X/Y/Z toggles + the colour swatch
		};
	} // namespace gui
} // namespace spades
