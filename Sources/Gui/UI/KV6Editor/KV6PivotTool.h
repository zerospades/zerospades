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
		// Set the model pivot: a draggable gizmo (0.1 steps) and a values prompt. The
		// sub-toolbar shows the live pivot position.
		class PivotTool : public ContainerTool {
		public:
			PivotTool();
			const char* Label() const override { return "Pivot"; }
			ToolOptions* Options() override { return &options; }
			// Refresh the readout from the live pivot, then forward to the sub-tool.
			void DrawScene(IEditorContext&) override;

		private:
			ToolOptions options; // a read-only pivot-position readout
		};
	} // namespace gui
} // namespace spades
