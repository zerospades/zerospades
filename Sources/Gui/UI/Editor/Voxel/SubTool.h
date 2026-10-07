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

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Components/Gizmo/TransformGizmo.h>

#include "ClickSequence.h"
#include "VoxelTool.h"
#include <Gui/UI/Editor/Shell/ToolEvent.h>

namespace spades {
	namespace gui {
		class IVoxelEditContext;

		// Leaf tools shown as buttons in the secondary toolbar (e.g. Select's Voxel
		// / Box). They are ordinary `VoxelTool`s with no children of their own; a
		// `ContainerTool` (Draw, Select) groups them and forwards input to the active
		// one. Throughout, the right button does the inverse of the left.

		// Single-voxel placement (Draw's "Voxel"): LMB places, RMB deletes.
		class DrawVoxelSubTool : public VoxelTool {
		public:
			const char* Label() const override { return "Voxel"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			void DrawScene(IVoxelEditContext&) override;
		};

		// Single-voxel recolour (Paint's "Voxel"): LMB recolours the hovered voxel
		// and keeps painting while dragged.
		class PaintVoxelSubTool : public VoxelTool {
		public:
			const char* Label() const override { return "Voxel"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			void DrawScene(IVoxelEditContext&) override;
		};

		// Single-voxel selection (Select's "Voxel"): LMB adds the voxel, RMB removes
		// it; LMB on empty space selects nothing.
		class SelectVoxelSubTool : public VoxelTool {
		public:
			const char* Label() const override { return "Voxel"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			void DrawScene(IVoxelEditContext&) override;
		};

		// Flood-fill selection by colour (Select's "By Colour"): LMB adds the
		// clicked voxel's colour region, RMB removes it; [L] adds the region under
		// the cursor.
		class ByColourSubTool : public VoxelTool {
		public:
			const char* Label() const override { return "By Colour"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			void OnKey(IVoxelEditContext&, const KeyInput&) override;
			void DrawScene(IVoxelEditContext&) override;
		};

		// What a finished shape (a box, a cylinder) does to its cells, and the
		// verb the hint names it by ("fill"). Injected by the container hosting
		// the shape, so Draw, Paint and Select share one shape tool; an action
		// without `apply` is none.
		struct CellAction {
			using ApplyFn = std::function<void(IVoxelEditContext&, const std::vector<IntVector3>&)>;
			ApplyFn apply;
			const char* verb = "";
		};

		// A 3-point axis-aligned box: corner, opposite corner (on the clicked face's
		// plane), then depth. The corner/depth are placed in free space, so the box
		// can be sized beyond the existing model. The three clicks are tracked by a
		// `ClickSequence`; what the box does to its cells (fill voxels, or add to
		// the selection) is injected, so Draw, Paint and Select reuse the same code.
		class BoxSubTool : public VoxelTool {
		public:
			using Action = CellAction;

			// `primary` runs when the final click is LMB, `secondary` when it is
			// RMB (e.g. fill vs erase, or select vs deselect). Without a secondary
			// action the right button does nothing. `mirrored` previews the box's
			// mirror images, for actions that reflect.
			BoxSubTool(Action primary, Action secondary, bool mirrored)
			    : primary(std::move(primary)), secondary(std::move(secondary)), mirrored(mirrored) {}

			const char* Label() const override { return "Box"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnActivate(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			std::string EscapeLabel(IVoxelEditContext&) override;
			void OnEscape(IVoxelEditContext&) override;
			// The recorded corners name voxels, and a volume that grows or is
			// trimmed renames every one of them: the box is dropped whenever the
			// document moves under it, or the editor acts behind the tool's back.
			void CancelInteraction(IVoxelEditContext&) override;
			void OnDocumentChanged(IVoxelEditContext&) override;
			void DrawScene(IVoxelEditContext&) override;

		private:
			Action primary;
			Action secondary;
			bool mirrored;

			ClickSequence seq;  // the corner / opposite-corner / depth clicks
			int normalAxis = 2; // axis of the clicked face's normal (set on click 1)

			// Construction point for the current stage (seq.Count() == 1 -> opposite
			// corner on the face plane; == 2 -> depth along the normal), placed in
			// free space so the box can be sized beyond existing voxels.
			bool StagePoint(IVoxelEditContext& ed, IntVector3& out) const;
			// Inclusive box spanned by the recorded points plus an in-progress one
			// (`pts` holds 2 or 3 points: corner, opposite corner, [depth]).
			void BBoxOf(const std::vector<IntVector3>& pts, IntVector3& lo, IntVector3& hi) const;
			void CellsOf(const std::vector<IntVector3>& pts, std::vector<IntVector3>& out) const;
		};

		// A 3-point circular cylinder: its centre on a voxel face, a point on its
		// rim (on that face's plane, setting the radius), then its depth along the
		// face's normal. The rim and the depth are placed in free space, so the
		// cylinder can reach beyond the existing model. Like Box, what it does to
		// its cells is injected by the container hosting it.
		class CylinderSubTool : public VoxelTool {
		public:
			using Action = CellAction;

			// `primary` runs when the final click is LMB, `secondary` when it is
			// RMB; without a secondary action the right button does nothing.
			CylinderSubTool(Action primary, Action secondary)
			    : primary(std::move(primary)), secondary(std::move(secondary)) {}

			const char* Label() const override { return "Cylinder"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnActivate(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			std::string EscapeLabel(IVoxelEditContext&) override;
			void OnEscape(IVoxelEditContext&) override;
			// The recorded points name voxels, which a reframe renames: the
			// cylinder is dropped whenever the document moves under it, or the
			// editor acts behind the tool's back.
			void CancelInteraction(IVoxelEditContext&) override;
			void OnDocumentChanged(IVoxelEditContext&) override;
			void DrawScene(IVoxelEditContext&) override;

		private:
			Action primary;
			Action secondary;

			ClickSequence seq;  // the centre / rim / depth clicks
			int normalAxis = 2; // axis of the clicked face's normal (set on click 1)

			// Construction point for the current stage (seq.Count() == 1 -> rim
			// point on the face plane; == 2 -> depth along the normal).
			bool StagePoint(IVoxelEditContext& ed, IntVector3& out) const;
			// Squared distance, in the face plane, from the centre to `rim`.
			int RadiusSq(const IntVector3& rim) const;
			// The voxels of the solid cylinder of squared radius `radSq` about the
			// centre, from the centre's layer to `depth`'s along the normal.
			void CellsOf(int radSq, const IntVector3& depth, std::vector<IntVector3>& out) const;
			// Outline of that cylinder between layers `nmin` and `nmax`: the edge of
			// the disc on both caps, joined along the normal.
			void DrawWire(IVoxelEditContext& ed, int radSq, int nmin, int nmax) const;
		};

		/**
		 * Base for sub-tools driven by a transform gizmo.
		 *
		 * Routes the pointer to the gizmo (a left drag moves it; a right click or
		 * Escape during a drag cancels), highlights and draws it, and leaves the
		 * subclass to say where it sits and what a drag does to the document.
		 */
		class GizmoSubTool : public VoxelTool {
		public:
			void OnActivate(IVoxelEditContext&) override;
			void OnDeactivate(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			std::string EscapeLabel(IVoxelEditContext&) override;
			void OnEscape(IVoxelEditContext&) override;
			void CancelInteraction(IVoxelEditContext&) override;
			// What the gizmo handles moved under it, so a drag in progress is void.
			void OnDocumentChanged(IVoxelEditContext&) override;
			void DrawOverlay(IVoxelEditContext&) override;

			/**
			 * Moves snap to multiples of `step`: moving by them, or with `toGrid`
			 * landing on them. Turns keep their own snap.
			 */
			void SetTranslationSnap(float step, bool toGrid);

		protected:
			/** The gizmo snaps by `snap` and shows (and responds to) `handles`. */
			explicit GizmoSubTool(const GizmoSnap& snap,
			                      const GizmoHandleSet& handles = GizmoHandleSet::Translation());

			TransformGizmo gizmo;

			/**
			 * Where the gizmo is now: the handled thing's position with whatever
			 * this drag already did to it applied. False hides the gizmo (nothing
			 * to handle), and cancels a drag in progress.
			 */
			virtual bool CurrentPose(IVoxelEditContext& ed, GizmoPose& pose) = 0;
			virtual void OnGizmoBegin(IVoxelEditContext&) {}
			/** The drag moved on; `gizmo.Total()` and `gizmo.Step()` hold the change. */
			virtual void OnGizmoDrag(IVoxelEditContext&) {}
			/** The drag was released after changing things by `total`. */
			virtual void OnGizmoEnd(IVoxelEditContext&, const GizmoTransform& total) { (void)total; }
			/** The drag was abandoned; `undo` reverses every step it reported. */
			virtual void OnGizmoCancel(IVoxelEditContext&, const GizmoTransform& undo) { (void)undo; }
			/**
			 * A left press landed off every handle: the user is done with the
			 * gizmo, so work it left pending is completed here.
			 */
			virtual void OnClickAway(IVoxelEditContext&) {}

		private:
			bool SyncPose(IVoxelEditContext& ed);
			void CancelDrag(IVoxelEditContext& ed);
		};

		/**
		 * Moves and turns voxels with the gizmo: its arrows and squares move
		 * them by whole voxels, its axis rings turn them by quarter turns about
		 * their pivot. The arrow keys move them too (Page Up/Down for the third
		 * axis).
		 *
		 * A drag only previews; its release moves the selected voxels in the
		 * document, as one undo step, and they stay selected. A paste or an
		 * import waits instead, positioned the same way, until a click away
		 * from the gizmo, Enter, leaving the tool, or another command that
		 * needs the document as it stands places it; Escape or the right
		 * button drops it.
		 */
		class TransformSubTool : public GizmoSubTool {
		public:
			TransformSubTool();
			const char* Label() const override { return "Transform"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnDeactivate(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			void OnKey(IVoxelEditContext&, const KeyInput&) override;
			void DrawScene(IVoxelEditContext&) override;

		protected:
			// A drag only previews: the voxels move once, on release.
			bool CurrentPose(IVoxelEditContext& ed, GizmoPose& pose) override;
			void OnGizmoEnd(IVoxelEditContext& ed, const GizmoTransform& total) override;
			// Places a waiting paste or import where it is.
			void OnClickAway(IVoxelEditContext& ed) override;
		};

		// Moves the model pivot with the gizmo, snapped as the Pivot tool sets it.
		// Voxels stay put. The pivot follows the drag live and is committed as one
		// undo step on release.
		class PivotGizmoSubTool : public GizmoSubTool {
		public:
			PivotGizmoSubTool();
			const char* Label() const override { return "Move"; }
			std::string Hint(IVoxelEditContext&) override;

		protected:
			bool CurrentPose(IVoxelEditContext& ed, GizmoPose& pose) override;
			void OnGizmoBegin(IVoxelEditContext& ed) override;
			void OnGizmoDrag(IVoxelEditContext& ed) override;
			void OnGizmoEnd(IVoxelEditContext& ed, const GizmoTransform& total) override;
			void OnGizmoCancel(IVoxelEditContext& ed, const GizmoTransform& undo) override;

		private:
			Vector3 startPivot; // pivot at the grab
		};

		// Moves the mirror planes with the gizmo, snapped as the Mirror tool sets it
		// (never finer than 0.5, the step at which a reflection actually shifts).
		// The planes follow the drag live and the move is committed as one undo
		// step on release, as the pivot's is.
		class MirrorGizmoSubTool : public GizmoSubTool {
		public:
			MirrorGizmoSubTool();
			const char* Label() const override { return "Move"; }
			std::string Hint(IVoxelEditContext&) override;

		protected:
			bool CurrentPose(IVoxelEditContext& ed, GizmoPose& pose) override;
			void OnGizmoBegin(IVoxelEditContext& ed) override;
			void OnGizmoDrag(IVoxelEditContext& ed) override;
			void OnGizmoEnd(IVoxelEditContext& ed, const GizmoTransform& total) override;
			void OnGizmoCancel(IVoxelEditContext& ed, const GizmoTransform& undo) override;

		private:
			Vector3 startPlane; // planes at the grab
		};
	} // namespace gui
} // namespace spades
