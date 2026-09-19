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
#include <string>

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
			constexpr float kQuarterTurn = 0.5F * M_PI_F;
			const char* const kGizmoDragHint = "  |  [RMB] cancel a drag";

			// Adds the colour region of voxel `h` to the selection, or removes it.
			void ApplyColourRegion(IEditorContext& ed, const IntVector3& h, bool remove) {
				const std::vector<IntVector3> region = ed.LinkedColorRegion(h.x, h.y, h.z);
				if (remove)
					ed.DeselectCells(region);
				else
					ed.SelectCells(region);
				ed.SetStatus(std::string(remove ? "Deselected " : "Selected ") +
				             std::to_string(region.size()) + " linked voxels");
			}

			// A gizmo that only moves, in steps of `step`.
			GizmoSnap TranslationSnap(float step) {
				GizmoSnap snap;
				snap.translation = step;
				return snap;
			}

			// Whole voxels, and quarter turns about the world axes: the only turns
			// that keep every voxel on a voxel. The view ring and the trackball turn
			// about any axis, so Transform leaves them out.
			GizmoSnap TransformSnap() {
				GizmoSnap snap = TranslationSnap(1.0F);
				snap.rotation = kQuarterTurn;
				return snap;
			}
			GizmoHandleSet TransformHandles() {
				return GizmoHandleSet::Translation() | GizmoHandleSet::Of(GizmoHandle::RotateX) |
				       GizmoHandleSet::Of(GizmoHandle::RotateY) |
				       GizmoHandleSet::Of(GizmoHandle::RotateZ);
			}

			// A gizmo translation snapped to whole voxels, as integers.
			IntVector3 WholeVoxels(const Vector3& v) {
				return IntVector3::Make(int(std::lround(v.x)), int(std::lround(v.y)),
				                        int(std::lround(v.z)));
			}

			// A gizmo change in whole voxels and quarter turns. The Transform gizmo
			// snaps to both and turns only about world axes, so a rotation there is
			// (axis * sin(a / 2), cos(a / 2)) for a multiple a of a quarter turn;
			// its largest imaginary part names the axis.
			PlacementTransform WholeStep(const GizmoTransform& change) {
				PlacementTransform t;
				t.shift = WholeVoxels(change.translation);
				const Vector4& q = change.rotation.v;
				const float parts[3] = {q.x, q.y, q.z};
				int axis = 0;
				for (int a = 1; a < 3; a++) {
					if (std::fabs(parts[a]) > std::fabs(parts[axis]))
						axis = a;
				}
				const float angle = 2.0F * std::atan2(parts[axis], q.w);
				t.axis = axis;
				t.quarterTurns = int(std::lround(angle / kQuarterTurn));
				return t;
			}
		} // namespace

		// --- DrawVoxelSubTool (Draw single) ----------------------------------

		std::string DrawVoxelSubTool::Hint(IEditorContext&) {
			return "[LMB] place  |  [RMB] delete";
		}
		void DrawVoxelSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (!e.IsDown())
				return;
			if (e.IsLeft())
				ed.PlaceCube();
			else if (e.IsRight())
				ed.DeleteCube();
		}
		void DrawVoxelSubTool::DrawScene(IEditorContext& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 p = ed.PickPlace(), h = ed.PickSolid();
			ed.DrawCellOutlineMirrored(p.x, p.y, p.z, ed.ColorToVec(ed.CurrentColor()));
			ed.DrawCellOutline(h.x, h.y, h.z, kTarget);
		}

		// --- PaintVoxelSubTool (Paint single) --------------------------------

		std::string PaintVoxelSubTool::Hint(IEditorContext&) {
			return "[LMB] recolour, drag to keep going";
		}
		void PaintVoxelSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			// A press/drag/release of LMB is one stroke, which the editor keeps as
			// one undo step however many voxels it recolours: recolour the hovered
			// voxel, and keep doing so while the button is dragged.
			if (!e.IsLeft() || !(e.IsDown() || e.IsDrag()))
				return;
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 h = ed.PickSolid();
			std::vector<IntVector3> cell(1, h);
			ed.PaintCells(cell, ed.CurrentColor());
		}
		void PaintVoxelSubTool::DrawScene(IEditorContext& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 h = ed.PickSolid();
			ed.DrawCellOutlineMirrored(h.x, h.y, h.z, ed.ColorToVec(ed.CurrentColor()));
		}

		// --- SelectVoxelSubTool (Select single) ------------------------------

		std::string SelectVoxelSubTool::Hint(IEditorContext&) {
			return "[LMB] select  |  [RMB] deselect  |  [LMB] on empty space selects nothing";
		}
		void SelectVoxelSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (!e.IsDown() || !(e.IsLeft() || e.IsRight()))
				return;
			ed.DoPick();
			if (!ed.HasPick()) {
				if (e.IsLeft())
					ed.ClearSelection();
				return;
			}
			const std::vector<IntVector3> cell(1, ed.PickSolid());
			if (e.IsLeft())
				ed.SelectCells(cell);
			else
				ed.DeselectCells(cell);
		}
		void SelectVoxelSubTool::DrawScene(IEditorContext& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 h = ed.PickSolid();
			ed.DrawCellOutline(h.x, h.y, h.z, ed.IsSelected(h.x, h.y, h.z) ? kSelected : kHover);
		}

		// --- ByColourSubTool (Select flood-fill) -----------------------------

		std::string ByColourSubTool::Hint(IEditorContext&) {
			return "[LMB] select a colour region  |  [RMB] deselect it  |  [L] select the one under "
			       "the cursor";
		}
		void ByColourSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (!e.IsDown() || !(e.IsLeft() || e.IsRight()))
				return;
			ed.DoPick();
			if (ed.HasPick())
				ApplyColourRegion(ed, ed.PickSolid(), e.IsRight());
			else if (e.IsLeft())
				ed.ClearSelection();
		}
		void ByColourSubTool::OnKey(IEditorContext& ed, const KeyInput& e) {
			if (e.IsDown() && EqualsIgnoringCase(e.key, "L")) {
				ed.DoPick();
				if (ed.HasPick())
					ApplyColourRegion(ed, ed.PickSolid(), false);
			}
		}
		void ByColourSubTool::DrawScene(IEditorContext& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 h = ed.PickSolid();
			ed.DrawCellOutline(h.x, h.y, h.z, kHover);
		}

		// --- BoxSubTool (axis-aligned box) -----------------------------------

		std::string BoxSubTool::Hint(IEditorContext&) {
			if (seq.Count() == 0)
				return "click a corner on a voxel face";
			if (seq.Count() == 1)
				return "click the opposite corner";
			std::string hint = std::string("click the depth: [LMB] ") + primary.verb;
			if (secondary.apply)
				hint += std::string("  |  [RMB] ") + secondary.verb;
			return hint;
		}

		void BoxSubTool::OnActivate(IEditorContext&) { seq.Reset(); }

		bool BoxSubTool::StagePoint(IEditorContext& ed, IntVector3& out) const {
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

		void BoxSubTool::BBoxOf(const std::vector<IntVector3>& pts, IntVector3& lo,
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

		void BoxSubTool::CellsOf(const std::vector<IntVector3>& pts,
		                          std::vector<IntVector3>& out) const {
			IntVector3 lo, hi;
			BBoxOf(pts, lo, hi);
			out.clear();
			for (int x = lo.x; x <= hi.x; x++)
			for (int y = lo.y; y <= hi.y; y++)
			for (int z = lo.z; z <= hi.z; z++)
				out.push_back(MakeIntVector3(x, y, z));
		}

		void BoxSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (!e.IsDown())
				return;
			const bool rmb = e.IsRight();
			if (!e.IsLeft() && !(rmb && secondary.apply))
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
				return;
			}

			IntVector3 q;
			if (!StagePoint(ed, q))
				return;
			if (!seq.Add(q))
				return; // still collecting (just set the opposite corner)
			// The final click's button decides the action. The box is finished
			// whatever the action makes of it, so it is reset first.
			std::vector<IntVector3> cells;
			CellsOf(seq.Points(), cells);
			seq.Reset();
			(rmb ? secondary : primary).apply(ed, cells);
		}

		std::string BoxSubTool::EscapeLabel(IEditorContext&) {
			return seq.Active() ? "cancel the box" : std::string();
		}

		void BoxSubTool::OnEscape(IEditorContext&) { seq.Reset(); }

		void BoxSubTool::DrawScene(IEditorContext& ed) {
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
			if (mirrored)
				ed.DrawBoxOutlineMirrored(lo, hi, kHover);
			else
				ed.DrawBoxOutline(lo, hi, kHover);
		}

		// --- GizmoSubTool (shared gizmo plumbing) ----------------------------

		GizmoSubTool::GizmoSubTool(const GizmoSnap& snap, const GizmoHandleSet& handles) {
			gizmo.SetSnap(snap);
			gizmo.SetVisibleHandles(handles);
			gizmo.SetEnabledHandles(handles);
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
				if (gizmo.IsDragging() || !SyncPose(ed))
					return;
				const GizmoView view = ed.GetGizmoView();
				// Off the handles is away from the gizmo. A press on a handle that
				// cannot be grabbed right now (an axis seen end-on) is not.
				if (gizmo.HandleAt(view, e.pos) == GizmoHandle::None)
					OnClickAway(ed);
				else if (gizmo.Begin(view, e.pos))
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

		std::string GizmoSubTool::EscapeLabel(IEditorContext&) {
			return gizmo.IsDragging() ? "cancel the drag" : std::string();
		}

		void GizmoSubTool::OnEscape(IEditorContext& ed) { CancelDrag(ed); }

		void GizmoSubTool::SetTranslationSnap(float step, bool toGrid) {
			GizmoSnap snap = gizmo.Snap();
			snap.translation = step;
			snap.translationToGrid = toGrid;
			gizmo.SetSnap(snap);
		}

		void GizmoSubTool::CancelInteraction(IEditorContext& ed) { CancelDrag(ed); }
		void GizmoSubTool::OnDocumentChanged(IEditorContext& ed) { CancelDrag(ed); }

		void GizmoSubTool::DrawOverlay(IEditorContext& ed) {
			if (!SyncPose(ed))
				return;
			gizmo.Hover(ed.GetGizmoView(), ed.CursorPos());
			ed.DrawGizmo(gizmo);
		}

		// --- TransformSubTool (position the pending placement) ---------------

		TransformSubTool::TransformSubTool() : GizmoSubTool(TransformSnap(), TransformHandles()) {}

		std::string TransformSubTool::Hint(IEditorContext& ed) {
			if (!ed.HasPlacement() && ed.SelectionCount() == 0)
				return "select some voxels first, or paste some";
			std::string hint = "drag an arrow to move, a ring to turn 90 degrees  |  [Arrows] "
			                   "[PgUp/PgDn] nudge";
			hint += kGizmoDragHint;
			if (ed.HasPlacement())
				hint += "  |  [LMB] away from the gizmo places them";
			return hint;
		}

		void TransformSubTool::OnDeactivate(IEditorContext& ed) {
			GizmoSubTool::OnDeactivate(ed);
			// Leaving the tool is what writes the voxels into the document.
			ed.ApplyPlacement();
		}

		bool TransformSubTool::CurrentPose(IEditorContext& ed, GizmoPose& pose) {
			IntVector3 pivot;
			if (!ed.TransformPivot(pivot))
				return false;
			// The placement itself stays put until release; the gizmo rides the
			// previewed change, turned axes included.
			const GizmoTransform& total = gizmo.Total();
			pose.position = VecOf(pivot) + total.translation;
			for (int a = 0; a < 3; a++)
				pose.axes[a] = total.rotation.Apply(AxisUnit(a));
			return true;
		}

		void TransformSubTool::OnGizmoEnd(IEditorContext& ed, const GizmoTransform& total) {
			ed.TransformPlacement(WholeStep(total)); // one undo step, still only pending
		}

		void TransformSubTool::OnClickAway(IEditorContext& ed) {
			// With nothing pending (a selection not yet moved) there is nothing to finish.
			if (ed.HasPlacement())
				ed.ApplyPlacement();
		}

		void TransformSubTool::OnKey(IEditorContext& ed, const KeyInput& e) {
			if (e.phase != KeyPhase::Down)
				return;
			PlacementTransform t;
			if (e.key == "Left") t.shift.x = -1;
			else if (e.key == "Right") t.shift.x = 1;
			else if (e.key == "Down") t.shift.y = -1;
			else if (e.key == "Up") t.shift.y = 1;
			else if (e.key == "PageDown") t.shift.z = -1;
			else if (e.key == "PageUp") t.shift.z = 1;
			else return;
			ed.TransformPlacement(t);
		}

		void TransformSubTool::DrawScene(IEditorContext& ed) {
			const PlacementTransform t = WholeStep(gizmo.Total());
			if (!t.IsIdentity())
				ed.DrawPlacementTransformed(t, MakeVector4(0.4F, 1.0F, 0.5F, 0.9F));
		}

		// --- PivotGizmoSubTool (drag the pivot) ------------------------------

		// The Pivot tool sets the snap (GizmoTool).
		PivotGizmoSubTool::PivotGizmoSubTool() : GizmoSubTool(GizmoSnap()) {}

		std::string PivotGizmoSubTool::Hint(IEditorContext&) {
			return std::string("drag a handle to move the pivot") + kGizmoDragHint;
		}

		bool PivotGizmoSubTool::CurrentPose(IEditorContext& ed, GizmoPose& pose) {
			pose.position = ed.GetPivot(); // live during a drag (the preview moved it)
			return true;
		}

		void PivotGizmoSubTool::OnGizmoBegin(IEditorContext& ed) { startPivot = ed.GetPivot(); }

		void PivotGizmoSubTool::OnGizmoDrag(IEditorContext& ed) {
			// A preview: the marker and the toolbar readout follow the drag, and
			// the release commits it as one undo step.
			ed.PreviewPivot(startPivot + gizmo.Total().translation);
		}

		void PivotGizmoSubTool::OnGizmoEnd(IEditorContext& ed, const GizmoTransform& total) {
			// One undo step from where the drag began; a drag that went nowhere
			// commits nothing and its preview ends with the press.
			if (!total.IsIdentity())
				ed.SetPivot(startPivot + total.translation);
		}

		void PivotGizmoSubTool::OnGizmoCancel(IEditorContext& ed, const GizmoTransform&) {
			ed.PreviewPivot(startPivot); // show the original pivot again, commit nothing
		}

		// --- MirrorGizmoSubTool (drag the mirror planes) ---------------------

		// The Mirror tool sets the snap (GizmoTool).
		MirrorGizmoSubTool::MirrorGizmoSubTool() : GizmoSubTool(GizmoSnap()) {}

		std::string MirrorGizmoSubTool::Hint(IEditorContext&) {
			return std::string("drag a handle to move the planes") + kGizmoDragHint;
		}

		bool MirrorGizmoSubTool::CurrentPose(IEditorContext& ed, GizmoPose& pose) {
			pose.position = ed.MirrorPlane();
			return true;
		}

		void MirrorGizmoSubTool::OnGizmoBegin(IEditorContext& ed) { startPlane = ed.MirrorPlane(); }

		void MirrorGizmoSubTool::OnGizmoDrag(IEditorContext& ed) {
			// The planes follow the drag live, without journaling each step.
			ed.PreviewMirrorPlane(startPlane + gizmo.Total().translation);
		}

		void MirrorGizmoSubTool::OnGizmoEnd(IEditorContext& ed, const GizmoTransform& total) {
			// One undo step from where the drag began; a drag that went nowhere
			// commits nothing and its preview ends with the press.
			if (!total.IsIdentity())
				ed.SetMirrorPlane(startPlane + total.translation);
		}

		void MirrorGizmoSubTool::OnGizmoCancel(IEditorContext& ed, const GizmoTransform&) {
			ed.PreviewMirrorPlane(startPlane); // show the original planes again, commit nothing
		}
	} // namespace gui
} // namespace spades
