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
		 * Its bar carries a Step of 1, 0.5 or 0.1 -- how far a drag moves what
		 * the tool holds, keeping wherever between the steps it already sat --
		 * and Snap to Grid.
		 *
		 * Snap to Grid is a one-off, not a mode: it rounds what the tool moves
		 * to the nearest whole voxel, once, as one undo step (SnapToGrid). It is
		 * for tidying up a position dragged roughly into place, so it latches
		 * nothing and leaves dragging alone; a drag after it moves by the step
		 * as it always did. A tool offers only the steps what it moves can take,
		 * and offers Snap to Grid only where what it moves can be off the grid
		 * at all.
		 */
		class GizmoTool : public ContainerTool {
		public:
			ToolOptions* Options() override { return &options; }
			// Subclasses overriding these call them first.
			void UpdateOptions(IVoxelEditContext& ed) override;
			void OnOptionToggled(IVoxelEditContext& ed, const std::string& id, bool value) override;
			void OnAction(IVoxelEditContext& ed, const std::string& id) override;
			// Whatever sets this tool's numbers holds to the step it is set to.
			float TransformStep() const override { return Step(); }

		protected:
			/**
			 * `gizmo` becomes the only sub-tool. `steps` are the steps (of 1, 0.5
			 * and 0.1) it can take, the first being the one it starts on. `grid`
			 * is the grid what it moves always sits on (0 for none): where that
			 * is a whole voxel, it is on the grid already whatever anyone does
			 * to it, and Snap to Grid is left off the bar.
			 */
			GizmoTool(std::unique_ptr<GizmoSubTool> gizmo, std::vector<float> steps, float grid);

			/** Adds Step, and Snap to Grid where it applies, at this point of the bar. */
			void AddSnapOptions();

			/** The step in use: what a drag moves by, and what a box of this
			 *  tool's steps by, so the two never disagree. */
			float Step() const { return step; }

			/** `v` with each coordinate on the nearest whole voxel. */
			static Vector3 OnGrid(const Vector3& v);
			/**
			 * Puts what this tool moves on the grid, as one undo step, and says
			 * where it went. Called by the Snap to Grid button; a tool that never
			 * offers it needs no override.
			 */
			virtual void SnapToGrid(IVoxelEditContext&) {}

			ToolOptions options;

		private:
			// The grid Snap to Grid rounds to: the whole voxels, which is where
			// the voxels themselves are.
			static constexpr float kGridStep = 1.0F;
			static constexpr float kGridAt = 0.0F;

			GizmoSubTool* gizmo; // owned by `subs`
			std::vector<float> steps;
			float grid;
			float step;

			bool Offers(float candidate) const;
			// Whether what this tool moves can be off the grid, and so whether
			// there is ever anything for Snap to Grid to do.
			bool GridSnapApplies() const;
			// Hands the current step to the gizmo.
			void ApplySnap();
		};
	} // namespace gui
} // namespace spades
