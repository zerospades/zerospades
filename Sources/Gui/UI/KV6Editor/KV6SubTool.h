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

#include "KV6ClickSequence.h"
#include "KV6EditorTool.h"
#include "KV6ToolEvent.h"

namespace spades {
	namespace gui {
		class IEditorContext;

		// Leaf tools shown as buttons in the secondary toolbar (e.g. Select's Voxel
		// / Box). They are ordinary `EditorTool`s with no children of their own; a
		// `ContainerTool` (Draw, Select) groups them and forwards input to the active
		// one. Throughout, the right button does the inverse of the left.

		// Single-voxel placement (Draw's "Voxel"): LMB places, RMB deletes.
		class DrawVoxelSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Voxel"; }
			std::string Hint(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void DrawScene(IEditorContext&) override;
		};

		// Single-voxel recolour (Paint's "Voxel"): LMB recolours the hovered voxel
		// and keeps painting while dragged.
		class PaintVoxelSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Voxel"; }
			std::string Hint(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void DrawScene(IEditorContext&) override;
		};

		// Single-voxel selection (Select's "Voxel"): LMB adds the voxel, RMB removes
		// it; LMB on empty space selects nothing.
		class SelectVoxelSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Voxel"; }
			std::string Hint(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void DrawScene(IEditorContext&) override;
		};

		// Flood-fill selection by colour (Select's "By Colour"): LMB adds the
		// clicked voxel's colour region, RMB removes it; [L] adds the region under
		// the cursor.
		class ByColourSubTool : public EditorTool {
		public:
			const char* Label() const override { return "By Colour"; }
			std::string Hint(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void OnKey(IEditorContext&, const KeyInput&) override;
			void DrawScene(IEditorContext&) override;
		};

		// A 3-point axis-aligned box: corner, opposite corner (on the clicked face's
		// plane), then depth. The corner/depth are placed in free space, so the box
		// can be sized beyond the existing model. The three clicks are tracked by a
		// `ClickSequence`; what the box does to its cells (fill voxels, or add to
		// the selection) is injected, so Draw, Paint and Select reuse the same code.
		class BoxSubTool : public EditorTool {
		public:
			using ApplyFn = std::function<void(IEditorContext&, const std::vector<IntVector3>&)>;

			// What a finished box does to its cells, and the verb the hint names
			// it by ("fill").
			struct Action {
				ApplyFn apply;
				const char* verb = "";
			};

			// `primary` runs when the final click is LMB, `secondary` when it is
			// RMB (e.g. fill vs erase, or select vs deselect). Without a secondary
			// action the right button does nothing. `mirrored` previews the box's
			// mirror images, for actions that reflect.
			BoxSubTool(Action primary, Action secondary, bool mirrored)
			    : primary(std::move(primary)), secondary(std::move(secondary)), mirrored(mirrored) {}

			const char* Label() const override { return "Box"; }
			std::string Hint(IEditorContext&) override;
			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			bool OnEscape(IEditorContext&) override;
			void DrawScene(IEditorContext&) override;

		private:
			Action primary;
			Action secondary;
			bool mirrored;

			ClickSequence seq;  // the corner / opposite-corner / depth clicks
			int normalAxis = 2; // axis of the clicked face's normal (set on click 1)

			// Construction point for the current stage (seq.Count() == 1 -> opposite
			// corner on the face plane; == 2 -> depth along the normal), placed in
			// free space so the box can be sized beyond existing voxels.
			bool StagePoint(IEditorContext& ed, IntVector3& out) const;
			// Inclusive box spanned by the recorded points plus an in-progress one
			// (`pts` holds 2 or 3 points: corner, opposite corner, [depth]).
			void BBoxOf(const std::vector<IntVector3>& pts, IntVector3& lo, IntVector3& hi) const;
			void CellsOf(const std::vector<IntVector3>& pts, std::vector<IntVector3>& out) const;
		};

		/**
		 * Base for sub-tools driven by a transform gizmo.
		 *
		 * Routes the pointer to the gizmo (a left drag moves it; a right click or
		 * Escape during a drag cancels), highlights and draws it, and leaves the
		 * subclass to say where it sits and what a drag does to the document.
		 */
		class GizmoSubTool : public EditorTool {
		public:
			void OnActivate(IEditorContext&) override;
			void OnDeactivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			bool OnEscape(IEditorContext&) override;
			void CancelInteraction(IEditorContext&) override;
			// What the gizmo handles moved under it, so a drag in progress is void.
			void OnDocumentChanged(IEditorContext&) override;
			void DrawOverlay(IEditorContext&) override;

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
			virtual bool CurrentPose(IEditorContext& ed, GizmoPose& pose) = 0;
			virtual void OnGizmoBegin(IEditorContext&) {}
			/** The drag moved on; `gizmo.Total()` and `gizmo.Step()` hold the change. */
			virtual void OnGizmoDrag(IEditorContext&) {}
			/** The drag was released after changing things by `total`. */
			virtual void OnGizmoEnd(IEditorContext&, const GizmoTransform& total) { (void)total; }
			/** The drag was abandoned; `undo` reverses every step it reported. */
			virtual void OnGizmoCancel(IEditorContext&, const GizmoTransform& undo) { (void)undo; }
			/**
			 * A left press landed off every handle: the user is done with the
			 * gizmo, so work it left pending is completed here.
			 */
			virtual void OnClickAway(IEditorContext&) {}

		private:
			bool SyncPose(IEditorContext& ed);
			void CancelDrag(IEditorContext& ed);
		};

		/**
		 * Positions pending voxels with the gizmo: its arrows and squares move
		 * them by whole voxels, its axis rings turn them by quarter turns about
		 * their pivot. The arrow keys move them too (Page Up/Down for the third
		 * axis).
		 *
		 * The first move or turn lifts the selected voxels out of the document;
		 * a paste or an import arrives already pending. Nothing is written back
		 * until a click away from the gizmo, Place, leaving the tool, or another
		 * command that needs the document as it stands, so voxels dragged over
		 * others never destroy them; Cancel or Escape puts them back instead.
		 * Every move and turn is an undo step. With nothing
		 * pending, the next move takes whatever is selected at that moment.
		 */
		class TransformSubTool : public GizmoSubTool {
		public:
			TransformSubTool();
			const char* Label() const override { return "Transform"; }
			std::string Hint(IEditorContext&) override;
			void OnDeactivate(IEditorContext&) override;
			void OnKey(IEditorContext&, const KeyInput&) override;
			bool OnEscape(IEditorContext&) override;
			void DrawScene(IEditorContext&) override;

		protected:
			// A drag only previews: the voxels move once, on release.
			bool CurrentPose(IEditorContext& ed, GizmoPose& pose) override;
			void OnGizmoEnd(IEditorContext& ed, const GizmoTransform& total) override;
			// Places the pending voxels where they are.
			void OnClickAway(IEditorContext& ed) override;
		};

		// Moves the model pivot with the gizmo, snapped as the Pivot tool sets it.
		// Voxels stay put. The pivot follows the drag live and is committed as one
		// undo step on release.
		class PivotGizmoSubTool : public GizmoSubTool {
		public:
			PivotGizmoSubTool();
			const char* Label() const override { return "Move"; }
			std::string Hint(IEditorContext&) override;

		protected:
			bool CurrentPose(IEditorContext& ed, GizmoPose& pose) override;
			void OnGizmoBegin(IEditorContext& ed) override;
			void OnGizmoDrag(IEditorContext& ed) override;
			void OnGizmoEnd(IEditorContext& ed, const GizmoTransform& total) override;
			void OnGizmoCancel(IEditorContext& ed, const GizmoTransform& undo) override;

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
			std::string Hint(IEditorContext&) override;

		protected:
			bool CurrentPose(IEditorContext& ed, GizmoPose& pose) override;
			void OnGizmoBegin(IEditorContext& ed) override;
			void OnGizmoDrag(IEditorContext& ed) override;
			void OnGizmoEnd(IEditorContext& ed, const GizmoTransform& total) override;
			void OnGizmoCancel(IEditorContext& ed, const GizmoTransform& undo) override;

		private:
			Vector3 startPlane; // planes at the grab
		};
	} // namespace gui
} // namespace spades
