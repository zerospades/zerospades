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

#include "GizmoTool.h"

namespace spades {
	namespace gui {
		// Set the model pivot: a draggable gizmo (steps down to 0.1), and its X,
		// Y and Z in the editor's transform panel, which show where it is and
		// are scrubbed or typed to move it.
		class PivotTool : public GizmoTool {
		public:
			PivotTool();
			const char* Label() const override { return "Pivot"; }
			ITransformTarget* TransformTarget(IVoxelEditContext& ed) override;
		};
	} // namespace gui
} // namespace spades
