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

#include "PivotTool.h"
#include "VoxelEditContext.h"

namespace spades {
	namespace gui {
		// The pivot is a point anywhere, on no grid: it takes every step, finest first.
		PivotTool::PivotTool()
		    : GizmoTool(std::unique_ptr<GizmoSubTool>(new PivotGizmoSubTool()), {0.1F, 0.5F, 1.0F},
		                0.0F) {
			AddSnapOptions();
		}

		// Where the pivot is, set by number: the transform panel's Location.
		ITransformTarget* PivotTool::TransformTarget(IVoxelEditContext& ed) {
			return &ed.PivotTransform();
		}
	} // namespace gui
} // namespace spades
