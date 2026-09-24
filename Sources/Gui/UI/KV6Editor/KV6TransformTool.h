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

#include "KV6GizmoTool.h"

namespace spades {
	namespace gui {
		/**
		 * Move and turn the selection, or voxels being pasted or imported.
		 *
		 * Its one sub-tool is the gizmo (see TransformSubTool). Place writes the
		 * pending voxels into the model and Cancel puts them back, so neither
		 * waits on leaving the tool. "Turn about" picks whether turns go round
		 * the middle of the voxels or the model's pivot.
		 *
		 * The sub-toolbar carries the same two moves as boxes. Move shows the
		 * voxel the pending ones are centred on, and typing or stepping it takes
		 * them there. Turn shows how far these voxels have been turned about
		 * each axis since they were lifted, whether by a box or by the gizmo,
		 * and typing or stepping it turns them the rest of the way. The record
		 * belongs to the pending voxels (see PendingPlacement::turns), so it
		 * goes back to zero as soon as they are no longer the voxels in hand —
		 * placed, dropped, or set aside by a change of selection.
		 */
		class TransformTool : public GizmoTool {
		public:
			TransformTool();
			const char* Label() const override { return "Transform"; }
			void UpdateOptions(IEditorContext& ed) override;
			void OnOptionToggled(IEditorContext& ed, const std::string& id, bool value) override;
			void OnOptionNumberChanged(IEditorContext& ed, const std::string& id, float value,
			                           bool committed) override;
			void OnAction(IEditorContext& ed, const std::string& id) override;
		};
	} // namespace gui
} // namespace spades
