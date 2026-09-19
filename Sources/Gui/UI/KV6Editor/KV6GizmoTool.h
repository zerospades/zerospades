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

#include <memory>
#include <vector>

#include "KV6ContainerTool.h"

namespace spades {
	namespace gui {
		/**
		 * A tool that moves something with a gizmo, its one sub-tool (Pivot,
		 * Mirror, Transform).
		 *
		 * Its bar carries the gizmo's snapping, a setting of the tool: a Step of
		 * 1, 0.5 or 0.1, and Snap to Grid, which lands what is moved on multiples
		 * of the step (with 1, on voxel centres) instead of moving it by them. A
		 * tool offers only the steps what it moves can take, and Snap to Grid
		 * only when it can be off the grid at all; the rest shows greyed out.
		 */
		class GizmoTool : public ContainerTool {
		public:
			ToolOptions* Options() override { return &options; }
			// Subclasses overriding these call them first.
			void UpdateOptions(IEditorContext& ed) override;
			void OnOptionToggled(IEditorContext& ed, const std::string& id, bool value) override;

		protected:
			/**
			 * `gizmo` becomes the only sub-tool. `steps` are the steps (of 1, 0.5
			 * and 0.1) it can take, the first being the one it starts on;
			 * `gridSnap` says whether Snap to Grid means anything for it.
			 */
			GizmoTool(std::unique_ptr<GizmoSubTool> gizmo, std::vector<float> steps, bool gridSnap);

			/** Adds Step and Snap to Grid at this point of the bar. */
			void AddSnapOptions();

			ToolOptions options;

		private:
			GizmoSubTool* gizmo; // owned by `subs`
			std::vector<float> steps;
			bool gridSnap;
			float step;
			bool toGrid = false;

			bool Offers(float candidate) const;
			// Hands the current step and grid setting to the gizmo.
			void ApplySnap();
		};
	} // namespace gui
} // namespace spades
