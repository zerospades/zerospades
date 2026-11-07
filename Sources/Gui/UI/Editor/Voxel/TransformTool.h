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
		/**
		 * Move and turn the selection, or a paste or an import waiting to be
		 * placed.
		 *
		 * Its one sub-tool is the gizmo (see TransformSubTool). A selection
		 * moves in the document itself, one undo step per drag; a paste or an
		 * import waits until a click away from the gizmo or Enter places it.
		 * "Turn about" picks whether turns go round the middle of the voxels or
		 * the model's pivot.
		 *
		 * The sub-toolbar carries the same moves. Move shows the voxel the
		 * moved ones are centred on, and typing or stepping it takes them
		 * there. Turn turns them a right angle about an axis, right-handed.
		 */
		class TransformTool : public GizmoTool {
		public:
			TransformTool();
			const char* Label() const override { return "Transform"; }
			void UpdateOptions(IVoxelEditContext& ed) override;
			void OnOptionToggled(IVoxelEditContext& ed, const std::string& id, bool value) override;
			void OnOptionNumberChanged(IVoxelEditContext& ed, const std::string& id, float value,
			                           bool committed) override;
			void OnAction(IVoxelEditContext& ed, const std::string& id) override;
		};
	} // namespace gui
} // namespace spades
