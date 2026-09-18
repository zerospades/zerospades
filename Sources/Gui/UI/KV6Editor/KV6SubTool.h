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

		// Leaf tools shown as buttons in the secondary toolbar (e.g. Select's Point
		// / Rect). They are ordinary `EditorTool`s with no children of their own; a
		// `ContainerTool` (Draw, Select) groups them and forwards input to the active
		// one.

		// Single-voxel placement / deletion (Draw's "Block"): LMB places (or samples
		// a colour with Alt / pick mode), RMB deletes.
		class BlockSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Block"; }
			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void DrawScene(IEditorContext&) override;
		};

		// Single-voxel recolour (Paint's "Block"): LMB recolours the hovered voxel
		// and keeps painting while dragged; RMB or Alt+LMB samples a colour.
		class PaintBlockSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Block"; }
			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void DrawScene(IEditorContext&) override;
		};

		// Single-voxel selection toggle (Select's "Point").
		class PointSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Point"; }
			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void DrawScene(IEditorContext&) override;
		};

		// Flood-fill selection by colour (Select's "By Colour"); also bound to [L].
		class ByColourSubTool : public EditorTool {
		public:
			const char* Label() const override { return "By Colour"; }
			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void OnKey(IEditorContext&, const KeyInput&) override;
			void DrawScene(IEditorContext&) override;
		};

		// A 3-point axis-aligned box: corner, opposite corner (on the clicked face's
		// plane), then depth. The corner/depth are placed in free space, so the box
		// can be sized beyond the existing model. The three clicks are tracked by a
		// `ClickSequence`; the action applied to the cells (fill voxels, or add to
		// the selection) is injected, so Draw and Select reuse the same code.
		class RectSubTool : public EditorTool {
		public:
			using ApplyFn = std::function<void(IEditorContext&, const std::vector<IntVector3>&)>;

			// `apply` runs when the final click is LMB, `applyAlt` when it is RMB
			// (e.g. fill vs cut, or select vs deselect).
			RectSubTool(const char* label, ApplyFn apply, ApplyFn applyAlt, bool useMirror = false,
			            const char* applyMsg = "Rect applied", const char* altMsg = "Rect cut")
			    : label(label), apply(std::move(apply)), applyAlt(std::move(applyAlt)),
			      useMirror(useMirror), applyMsg(applyMsg), altMsg(altMsg) {}

			const char* Label() const override { return label; }
			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			bool OnEscape(IEditorContext&) override;
			void DrawScene(IEditorContext&) override;

		private:
			const char* label;
			ApplyFn apply;
			ApplyFn applyAlt;
			bool useMirror;
			const char* applyMsg; // status shown after an LMB apply
			const char* altMsg;   // status shown after an RMB (alt) apply

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
		 * Entering the tool lifts the selection into a placement, and a paste or an
		 * import arrives with one already pending. Nothing is written to the
		 * document until the tool is left, so voxels dragged over others never
		 * destroy them; Escape drops the placement instead. Any other command
		 * (select all, copy, save, undo, ...) lands the placement first and then
		 * the tool lifts the selection again: while it is active, what is selected
		 * is what moves.
		 */
		class TransformSubTool : public GizmoSubTool {
		public:
			TransformSubTool();
			const char* Label() const override { return "Transform"; }
			void OnActivate(IEditorContext&) override;
			void OnDeactivate(IEditorContext&) override;
			void OnDocumentChanged(IEditorContext&) override;
			void OnKey(IEditorContext&, const KeyInput&) override;
			bool OnEscape(IEditorContext&) override;
			void DrawScene(IEditorContext&) override;

		protected:
			// A drag only previews: the placement moves once, on release.
			bool CurrentPose(IEditorContext& ed, GizmoPose& pose) override;
			void OnGizmoEnd(IEditorContext& ed, const GizmoTransform& total) override;
		};

		// Moves the model pivot with the gizmo, in 0.1 steps. Voxels stay put. The
		// pivot follows the drag live and is committed as one undo step on release.
		class PivotGizmoSubTool : public GizmoSubTool {
		public:
			PivotGizmoSubTool();
			const char* Label() const override { return "Gizmo"; }
			void OnActivate(IEditorContext&) override;

		protected:
			bool CurrentPose(IEditorContext& ed, GizmoPose& pose) override;
			void OnGizmoBegin(IEditorContext& ed) override;
			void OnGizmoDrag(IEditorContext& ed) override;
			void OnGizmoEnd(IEditorContext& ed, const GizmoTransform& total) override;
			void OnGizmoCancel(IEditorContext& ed, const GizmoTransform& undo) override;

		private:
			Vector3 startPivot; // pivot at the grab
		};

		// Moves the mirror planes with the gizmo, in 0.5 steps — the step at which
		// a reflection actually shifts. The planes follow the drag live.
		class MirrorGizmoSubTool : public GizmoSubTool {
		public:
			MirrorGizmoSubTool();
			const char* Label() const override { return "Move"; }
			void OnActivate(IEditorContext&) override;

		protected:
			bool CurrentPose(IEditorContext& ed, GizmoPose& pose) override;
			// Applied step by step, so a resize shifting the planes mid-drag stands.
			void OnGizmoDrag(IEditorContext& ed) override;
			void OnGizmoCancel(IEditorContext& ed, const GizmoTransform& undo) override;
		};

		// Set the pivot by typing exact values into a prompt.
		class PivotValuesSubTool : public EditorTool {
		public:
			const char* Label() const override { return "Values"; }
			void OnActivate(IEditorContext&) override;
			void OnPointer(IEditorContext&, const PointerInput&) override;
			void DrawScene(IEditorContext&) override;
		};
	} // namespace gui
} // namespace spades
