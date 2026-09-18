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

#include "KV6SubTool.h"
#include "KV6EditorContext.h"

#include <algorithm>
#include <cmath>

namespace spades {
	namespace gui {
		namespace {
			int Comp(const IntVector3& v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }
			void SetComp(IntVector3& v, int a, int val) {
				if (a == 0) v.x = val;
				else if (a == 1) v.y = val;
				else v.z = val;
			}
			Vector3 VecOf(const IntVector3& v) {
				return MakeVector3(float(v.x), float(v.y), float(v.z));
			}
			Vector3 AxisUnit(int a) {
				return MakeVector3(a == 0 ? 1.0F : 0.0F, a == 1 ? 1.0F : 0.0F, a == 2 ? 1.0F : 0.0F);
			}
			const Vector4 kHover = MakeVector4(0.3F, 0.8F, 1.0F, 0.95F);
			const Vector4 kSelected = MakeVector4(1.0F, 0.3F, 0.3F, 0.95F);
			const Vector4 kTarget = MakeVector4(1.0F, 0.9F, 0.3F, 0.9F);
			const Vector4 kAxisCol[3] = {MakeVector4(1.0F, 0.35F, 0.35F, 1.0F),
			                             MakeVector4(0.4F, 1.0F, 0.4F, 1.0F),
			                             MakeVector4(0.45F, 0.6F, 1.0F, 1.0F)};
			// Gizmo snap steps for turning and scaling, for the tools that enable
			// those handles.
			const float kRotationStep = 15.0F * M_PI_F / 180.0F;
			const float kScaleStep = 0.1F;

			// A gizmo translation snapped to whole voxels, as integers.
			IntVector3 WholeVoxels(const Vector3& v) {
				return IntVector3::Make(int(std::lround(v.x)), int(std::lround(v.y)),
				                        int(std::lround(v.z)));
			}
		} // namespace

		// --- BlockSubTool (Draw single) --------------------------------------

		void BlockSubTool::OnActivate(IEditorContext& ed) {
			ed.SetStatus("Draw: LMB place  -  RMB delete  -  Alt+LMB pick colour");
		}
		void BlockSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (!e.IsDown())
				return;
			if (e.IsLeft()) {
				if (e.alt || ed.PickModeActive()) {
					ed.Eyedropper();
					ed.ClearPickMode();
					return;
				}
				ed.PlaceCube();
			} else if (e.IsRight()) {
				ed.DeleteCube();
			}
		}
		void BlockSubTool::DrawScene(IEditorContext& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 p = ed.PickPlace(), h = ed.PickSolid();
			ed.DrawCellOutlineMirrored(p.x, p.y, p.z, ed.ColorToVec(ed.CurrentColor()));
			ed.DrawCellOutline(h.x, h.y, h.z, kTarget);
		}

		// --- PaintBlockSubTool (Paint single) --------------------------------

		void PaintBlockSubTool::OnActivate(IEditorContext& ed) {
			ed.SetStatus("Paint: LMB drag to recolour  -  RMB / Alt+LMB sample colour");
		}
		void PaintBlockSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			// A press/drag/release of LMB is one stroke = one undo step; the inner
			// PaintCells calls coalesce inside this group.
			if (e.IsLeft() && e.IsUp()) {
				ed.EndUndoGroup();
				return;
			}
			// Sample a colour (Alt, pick mode, or RMB), else recolour the hovered
			// voxel — and keep doing so while the button is dragged.
			if (e.IsRight() && e.IsDown()) {
				ed.Eyedropper();
				return;
			}
			if (!e.IsLeft() || !(e.IsDown() || e.IsDrag()))
				return;
			if (e.IsDown())
				ed.BeginUndoGroup("Paint");
			if (e.alt || ed.PickModeActive()) {
				ed.Eyedropper();
				ed.ClearPickMode();
				return;
			}
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 h = ed.PickSolid();
			std::vector<IntVector3> cell(1, h);
			ed.PaintCells(cell, ed.CurrentColor());
		}
		void PaintBlockSubTool::DrawScene(IEditorContext& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 h = ed.PickSolid();
			ed.DrawCellOutlineMirrored(h.x, h.y, h.z, ed.ColorToVec(ed.CurrentColor()));
		}

		// --- PointSubTool (Select single) ------------------------------------

		void PointSubTool::OnActivate(IEditorContext& ed) {
			ed.SetStatus("Select Point: click to (de)select  -  click empty to clear");
		}
		void PointSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (!e.IsDown() || !e.IsLeft())
				return;
			ed.DoPick();
			if (ed.HasPick()) {
				IntVector3 h = ed.PickSolid();
				ed.ToggleSelect(h.x, h.y, h.z);
			} else {
				ed.ClearSelection();
			}
		}
		void PointSubTool::DrawScene(IEditorContext& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 h = ed.PickSolid();
			ed.DrawCellOutline(h.x, h.y, h.z, ed.IsSelected(h.x, h.y, h.z) ? kSelected : kHover);
		}

		// --- ByColourSubTool (Select flood-fill) -----------------------------

		void ByColourSubTool::OnActivate(IEditorContext& ed) {
			ed.SetStatus("Select By Colour: click a voxel to select its colour region  -  [L]");
		}
		void ByColourSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (!e.IsDown() || !e.IsLeft())
				return;
			ed.DoPick();
			if (ed.HasPick()) {
				IntVector3 h = ed.PickSolid();
				ed.SelectLinkedColor(h.x, h.y, h.z);
			} else {
				ed.ClearSelection();
			}
		}
		void ByColourSubTool::OnKey(IEditorContext& ed, const KeyInput& e) {
			if (e.IsDown() && EqualsIgnoringCase(e.key, "L")) {
				ed.DoPick();
				if (ed.HasPick()) {
					IntVector3 h = ed.PickSolid();
					ed.SelectLinkedColor(h.x, h.y, h.z);
				}
			}
		}
		void ByColourSubTool::DrawScene(IEditorContext& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 h = ed.PickSolid();
			ed.DrawCellOutline(h.x, h.y, h.z, kHover);
		}

		// --- RectSubTool (axis-aligned box) ----------------------------------

		void RectSubTool::OnActivate(IEditorContext& ed) {
			seq.Reset();
			ed.SetStatus("Rect: click a corner on a voxel face");
		}

		bool RectSubTool::StagePoint(IEditorContext& ed, IntVector3& out) const {
			const IntVector3& p0 = seq.Points()[0];
			Vector3 pp = VecOf(p0);
			// Opposite corner: free on the face plane. Depth: free along the normal
			// (use only the normal-axis component of a view-facing plane pick).
			if (seq.Count() == 1)
				return ed.RayPlaneCell(pp, AxisUnit(normalAxis), out);
			IntVector3 q;
			if (!ed.RayPlaneCell(pp, ed.ViewDir(), q))
				return false;
			out = p0;
			SetComp(out, normalAxis, Comp(q, normalAxis));
			return true;
		}

		void RectSubTool::BBoxOf(const std::vector<IntVector3>& pts, IntVector3& lo,
		                         IntVector3& hi) const {
			int na = normalAxis, u = (na + 1) % 3, v = (na + 2) % 3;
			const IntVector3& p0 = pts[0];
			const IntVector3& p1 = pts[1]; // opposite corner -> in-plane extent
			SetComp(lo, u, std::min(Comp(p0, u), Comp(p1, u)));
			SetComp(hi, u, std::max(Comp(p0, u), Comp(p1, u)));
			SetComp(lo, v, std::min(Comp(p0, v), Comp(p1, v)));
			SetComp(hi, v, std::max(Comp(p0, v), Comp(p1, v)));
			if (pts.size() >= 3) { // depth point -> extent along the normal
				SetComp(lo, na, std::min(Comp(p0, na), Comp(pts[2], na)));
				SetComp(hi, na, std::max(Comp(p0, na), Comp(pts[2], na)));
			} else { // single layer until the depth is picked
				SetComp(lo, na, Comp(p0, na));
				SetComp(hi, na, Comp(p0, na));
			}
		}

		void RectSubTool::CellsOf(const std::vector<IntVector3>& pts,
		                          std::vector<IntVector3>& out) const {
			IntVector3 lo, hi;
			BBoxOf(pts, lo, hi);
			out.clear();
			for (int x = lo.x; x <= hi.x; x++)
			for (int y = lo.y; y <= hi.y; y++)
			for (int z = lo.z; z <= hi.z; z++)
				out.push_back(MakeIntVector3(x, y, z));
		}

		void RectSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (!e.IsDown())
				return;
			bool lmb = e.IsLeft(), rmb = e.IsRight();
			if (!lmb && !rmb)
				return;

			// First click: anchor a corner on a solid face and capture the normal.
			if (seq.Count() == 0) {
				ed.DoPick();
				if (!ed.HasPick())
					return;
				IntVector3 p0 = ed.PickSolid();
				IntVector3 d = ed.PickPlace() - p0;
				normalAxis = (d.x != 0) ? 0 : (d.y != 0) ? 1 : 2;
				seq.BeginFixed(3);
				seq.Add(p0);
				ed.SetStatus("Rect: pick the opposite corner  (RMB on the last click cuts)");
				return;
			}

			IntVector3 q;
			if (!StagePoint(ed, q))
				return;
			if (!seq.Add(q)) { // still collecting (just set the opposite corner)
				ed.SetStatus("Rect: pick the depth  (RMB to cut)");
				return;
			}
			// The final click's button decides the action: LMB = apply, RMB = alt.
			std::vector<IntVector3> cells;
			CellsOf(seq.Points(), cells);
			if (rmb)
				applyAlt(ed, cells);
			else
				apply(ed, cells);
			seq.Reset();
			ed.SetStatus(rmb ? altMsg : applyMsg);
		}

		bool RectSubTool::OnEscape(IEditorContext& ed) {
			if (!seq.Active())
				return false;
			seq.Reset();
			ed.SetStatus("Rect cancelled");
			return true;
		}

		void RectSubTool::DrawScene(IEditorContext& ed) {
			if (seq.Count() == 0) {
				ed.DoPick();
				if (ed.HasPick()) {
					IntVector3 h = ed.PickSolid();
					ed.DrawCellOutline(h.x, h.y, h.z, kHover);
				}
				return;
			}
			IntVector3 q;
			if (!StagePoint(ed, q)) {
				const IntVector3& p0 = seq.Points()[0];
				ed.DrawCellOutline(p0.x, p0.y, p0.z, kHover);
				return;
			}
			std::vector<IntVector3> pts = seq.Points();
			pts.push_back(q); // include the in-progress point
			IntVector3 lo, hi;
			BBoxOf(pts, lo, hi);
			if (useMirror)
				ed.DrawBoxOutlineMirrored(lo, hi, kHover);
			else
				ed.DrawBoxOutline(lo, hi, kHover);
		}

		// --- GizmoSubTool (shared gizmo plumbing) ----------------------------

		GizmoSubTool::GizmoSubTool(float translationStep) {
			GizmoSnap snap;
			snap.translation = translationStep;
			snap.rotation = kRotationStep;
			snap.scale = kScaleStep;
			gizmo.SetSnap(snap);
		}

		bool GizmoSubTool::SyncPose(IEditorContext& ed) {
			GizmoPose pose;
			if (!CurrentPose(ed, pose))
				return false;
			gizmo.SetPose(pose);
			return true;
		}

		void GizmoSubTool::CancelDrag(IEditorContext& ed) {
			if (gizmo.IsDragging())
				OnGizmoCancel(ed, gizmo.Cancel());
		}

		// Whatever happened while the tool was away, a drag never outlives it.
		void GizmoSubTool::OnActivate(IEditorContext& ed) { CancelDrag(ed); }
		void GizmoSubTool::OnDeactivate(IEditorContext& ed) { CancelDrag(ed); }

		void GizmoSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (e.IsRight() && e.IsDown()) {
				CancelDrag(ed); // as in Blender: a right click abandons the drag
				return;
			}
			if (!e.IsLeft())
				return;
			if (e.IsDown()) {
				if (!gizmo.IsDragging() && SyncPose(ed) && gizmo.Begin(ed.GetGizmoView(), e.pos))
					OnGizmoBegin(ed);
			} else if (e.IsDrag()) {
				if (!gizmo.IsDragging())
					return;
				if (!SyncPose(ed)) {
					CancelDrag(ed); // what was being handled is gone
					return;
				}
				if (gizmo.Drag(ed.GetGizmoView(), e.pos))
					OnGizmoDrag(ed);
			} else if (e.IsUp()) {
				if (gizmo.IsDragging())
					OnGizmoEnd(ed, gizmo.End());
			}
		}

		bool GizmoSubTool::OnEscape(IEditorContext& ed) {
			if (!gizmo.IsDragging())
				return false;
			CancelDrag(ed);
			return true;
		}

		void GizmoSubTool::CancelInteraction(IEditorContext& ed) { CancelDrag(ed); }

		void GizmoSubTool::DrawOverlay(IEditorContext& ed) {
			if (!SyncPose(ed))
				return;
			gizmo.Hover(ed.GetGizmoView(), ed.CursorPos());
			ed.DrawGizmo(gizmo);
		}

		// --- MoveSubTool (position the pending placement) --------------------

		MoveSubTool::MoveSubTool() : GizmoSubTool(1.0F) {}

		void MoveSubTool::OnActivate(IEditorContext& ed) {
			GizmoSubTool::OnActivate(ed);
			if (ed.HasPlacement())
				return; // a paste or import is already waiting to be positioned
			if (ed.BeginPlacementFromSelection())
				ed.SetStatus("Move: drag a handle or use the arrows; leaving Move applies it");
			else
				ed.SetStatus("Move: select some voxels first");
		}

		void MoveSubTool::OnDeactivate(IEditorContext& ed) {
			GizmoSubTool::OnDeactivate(ed);
			// Leaving the tool is what writes the voxels into the document.
			ed.ApplyPlacement();
		}

		bool MoveSubTool::CurrentPose(IEditorContext& ed, GizmoPose& pose) {
			Vector3 centroid;
			if (!ed.PlacementCentroid(centroid))
				return false;
			// The placement itself stays put until release; the gizmo rides the
			// previewed offset.
			pose.position = centroid + gizmo.Total().translation;
			return true;
		}

		void MoveSubTool::OnGizmoEnd(IEditorContext& ed, const GizmoTransform& total) {
			IntVector3 d = WholeVoxels(total.translation);
			if (d.x != 0 || d.y != 0 || d.z != 0)
				ed.MovePlacement(d.x, d.y, d.z); // still only a pending move
		}

		void MoveSubTool::OnKey(IEditorContext& ed, const KeyInput& e) {
			if (e.phase != KeyPhase::Down || !ed.HasPlacement())
				return;
			int d[3] = {0, 0, 0};
			if (e.key == "Left") d[0] = -1;
			else if (e.key == "Right") d[0] = 1;
			else if (e.key == "Down") d[1] = -1;
			else if (e.key == "Up") d[1] = 1;
			else if (e.key == "PageDown") d[2] = -1;
			else if (e.key == "PageUp") d[2] = 1;
			else return;
			ed.MovePlacement(d[0], d[1], d[2]);
		}

		bool MoveSubTool::OnEscape(IEditorContext& ed) {
			if (GizmoSubTool::OnEscape(ed))
				return true; // cancelled the drag, the placement stays where it was
			if (ed.HasPlacement()) {
				ed.CancelPlacement(); // nothing was written, so nothing to undo
				return true;
			}
			return false;
		}

		void MoveSubTool::DrawScene(IEditorContext& ed) {
			IntVector3 d = WholeVoxels(gizmo.Total().translation);
			if (d.x != 0 || d.y != 0 || d.z != 0)
				ed.DrawPlacementOffset(d.x, d.y, d.z, MakeVector4(0.4F, 1.0F, 0.5F, 0.9F));
		}

		// --- PivotGizmoSubTool (drag the pivot) ------------------------------

		PivotGizmoSubTool::PivotGizmoSubTool() : GizmoSubTool(0.1F) {}

		void PivotGizmoSubTool::OnActivate(IEditorContext& ed) {
			GizmoSubTool::OnActivate(ed);
			ed.SetStatus("Pivot: drag a handle to move the pivot (0.1 steps)");
		}

		bool PivotGizmoSubTool::CurrentPose(IEditorContext& ed, GizmoPose& pose) {
			pose.position = ed.GetPivot(); // live during a drag (the preview moved it)
			return true;
		}

		void PivotGizmoSubTool::OnGizmoBegin(IEditorContext& ed) { startPivot = ed.GetPivot(); }

		void PivotGizmoSubTool::OnGizmoDrag(IEditorContext& ed) {
			// Move the pivot for real (marker and toolbar readout follow) but
			// without journaling it.
			ed.PreviewPivot(startPivot + gizmo.Total().translation);
		}

		void PivotGizmoSubTool::OnGizmoEnd(IEditorContext& ed, const GizmoTransform& total) {
			ed.PreviewPivot(startPivot); // rewind the preview...
			if (!total.IsIdentity())
				ed.SetPivot(startPivot + total.translation); // ...then apply as one step
		}

		void PivotGizmoSubTool::OnGizmoCancel(IEditorContext& ed, const GizmoTransform&) {
			ed.PreviewPivot(startPivot); // restore the original pivot, commit nothing
		}

		// --- MirrorGizmoSubTool (drag the mirror planes) ---------------------

		MirrorGizmoSubTool::MirrorGizmoSubTool() : GizmoSubTool(0.5F) {}

		void MirrorGizmoSubTool::OnActivate(IEditorContext& ed) {
			GizmoSubTool::OnActivate(ed);
			ed.SetStatus("Mirror: drag a handle to move the planes (0.5 steps)");
		}

		bool MirrorGizmoSubTool::CurrentPose(IEditorContext& ed, GizmoPose& pose) {
			pose.position = ed.MirrorPlane();
			return true;
		}

		void MirrorGizmoSubTool::OnGizmoDrag(IEditorContext& ed) {
			// The planes are editor state, not document state: nothing to journal.
			ed.SetMirrorPlane(ed.MirrorPlane() + gizmo.Step().translation);
		}

		void MirrorGizmoSubTool::OnGizmoCancel(IEditorContext& ed, const GizmoTransform& undo) {
			ed.SetMirrorPlane(ed.MirrorPlane() + undo.translation);
		}

		// --- PivotValuesSubTool (type the pivot) -----------------------------

		void PivotValuesSubTool::OnActivate(IEditorContext& ed) {
			ed.SetStatus("Pivot: type x y z, then [Enter]");
			ed.BeginPivotEntry();
		}
		void PivotValuesSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (e.IsDown() && e.IsLeft())
				ed.BeginPivotEntry(); // re-open the prompt
		}
		void PivotValuesSubTool::DrawScene(IEditorContext& ed) {
			Vector3 c = ed.GetPivot(); // mark where the pivot currently is
			for (int a = 0; a < 3; a++)
				ed.DrawLine3D(c - AxisUnit(a), c + AxisUnit(a), kAxisCol[a]);
		}
	} // namespace gui
} // namespace spades
