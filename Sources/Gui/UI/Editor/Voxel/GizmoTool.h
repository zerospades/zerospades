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

#include "ContainerTool.h"

namespace spades {
	namespace gui {
		/**
		 * A tool that moves something with a gizmo, its one sub-tool (Pivot,
		 * Mirror, Transform).
		 *
		 * Its bar carries the gizmo's snapping, a setting of the tool: a Step of
		 * 1, 0.5 or 0.1, which is how far a drag moves what it holds, and Snap
		 * to Grid, which instead lands it on the voxel corners -- the lattice a
		 * voxel apart that the editor draws on its ground -- whatever the step
		 * is. From a corner, a step of 0.5 reaches a voxel's centre in one
		 * press, which is the other place a pivot or a mirror plane belongs.
		 *
		 * A tool offers only the steps what it moves can take, and Snap to Grid
		 * only where what it moves can sit on a corner at all; the rest shows
		 * greyed out.
		 */
		class GizmoTool : public ContainerTool {
		public:
			ToolOptions* Options() override { return &options; }
			// Subclasses overriding these call them first.
			void UpdateOptions(IVoxelEditContext& ed) override;
			void OnOptionToggled(IVoxelEditContext& ed, const std::string& id, bool value) override;
			// Whatever sets this tool's numbers holds to the step it is set to.
			float TransformStep() const override { return Step(); }

		protected:
			/**
			 * `gizmo` becomes the only sub-tool. `steps` are the steps (of 1, 0.5
			 * and 0.1) it can take, the first being the one it starts on. `grid`
			 * is the grid what it moves always sits on (0 for none): where that
			 * is a whole voxel, it can never sit on a corner and Snap to Grid is
			 * greyed out.
			 */
			GizmoTool(std::unique_ptr<GizmoSubTool> gizmo, std::vector<float> steps, float grid);

			/** Adds Step and Snap to Grid at this point of the bar. */
			void AddSnapOptions();

			/** The step in use: what a drag moves by, and what a box of this
			 *  tool's steps by, so the two never disagree. */
			float Step() const { return step; }

			ToolOptions options;

		private:
			// The grid Snap to Grid lands on: the voxel corners, a voxel apart,
			// through -0.5 (a voxel spans i - 0.5 to i + 0.5). See GridSnapApplies.
			static constexpr float kGridStep = 1.0F;
			static constexpr float kGridAt = -0.5F;

			GizmoSubTool* gizmo; // owned by `subs`
			std::vector<float> steps;
			float grid;
			float step;
			bool toGrid = false;

			bool Offers(float candidate) const;
			// Whether Snap to Grid changes anything at the current step.
			bool GridSnapApplies() const;
			// Hands the current step and grid setting to the gizmo.
			void ApplySnap();
		};
	} // namespace gui
} // namespace spades
