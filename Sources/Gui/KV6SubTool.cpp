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
			const float kGizLen = 7.0F; // gizmo handle length (voxels)

			// Distance from point p to the segment [a, b] in screen space.
			float DistToSeg(const Vector2& p, const Vector2& a, const Vector2& b) {
				Vector2 ab = b - a;
				float l2 = Vector2::Dot(ab, ab);
				float t = (l2 < 1.0e-6F) ? 0.0F : Vector2::Dot(p - a, ab) / l2;
				t = std::max(0.0F, std::min(1.0F, t));
				return (p - (a + ab * t)).GetLength();
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
			Reset();
			ed.SetStatus("Rect: click a corner on a voxel face");
		}

		bool RectSubTool::ShapeCur(IEditorContext& ed, IntVector3& out) const {
			Vector3 pp = VecOf(p1);
			// Opposite corner: free on the face plane. Depth: free along the normal
			// (use only the normal-axis component of a view-facing plane pick).
			if (stage == 1)
				return ed.RayPlaneCell(pp, AxisUnit(normalAxis), out);
			IntVector3 q;
			if (!ed.RayPlaneCell(pp, ed.ViewDir(), q))
				return false;
			out = p1;
			SetComp(out, normalAxis, Comp(q, normalAxis));
			return true;
		}

		void RectSubTool::BBox(const IntVector3& cur, IntVector3& lo, IntVector3& hi) const {
			int na = normalAxis, u = (na + 1) % 3, v = (na + 2) % 3;
			if (stage == 1) {
				SetComp(lo, na, Comp(p1, na)); SetComp(hi, na, Comp(p1, na));
				SetComp(lo, u, std::min(Comp(p1, u), Comp(cur, u)));
				SetComp(hi, u, std::max(Comp(p1, u), Comp(cur, u)));
				SetComp(lo, v, std::min(Comp(p1, v), Comp(cur, v)));
				SetComp(hi, v, std::max(Comp(p1, v), Comp(cur, v)));
			} else {
				SetComp(lo, u, Comp(rectLo, u)); SetComp(hi, u, Comp(rectHi, u));
				SetComp(lo, v, Comp(rectLo, v)); SetComp(hi, v, Comp(rectHi, v));
				SetComp(lo, na, std::min(Comp(p1, na), Comp(cur, na)));
				SetComp(hi, na, std::max(Comp(p1, na), Comp(cur, na)));
			}
		}

		void RectSubTool::Cells(const IntVector3& cur, std::vector<IntVector3>& out) const {
			IntVector3 lo, hi;
			BBox(cur, lo, hi);
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

			if (stage == 0) {
				ed.DoPick();
				if (!ed.HasPick()) { Reset(); return; }
				p1 = ed.PickSolid();
				IntVector3 d = ed.PickPlace() - p1;
				normalAxis = (d.x != 0) ? 0 : (d.y != 0) ? 1 : 2;
				stage = 1;
				ed.SetStatus("Rect: pick the opposite corner  (RMB on the last click cuts)");
				return;
			}

			IntVector3 q;
			if (!ShapeCur(ed, q))
				return;
			if (stage == 1) {
				int na = normalAxis, u = (na + 1) % 3, v = (na + 2) % 3;
				SetComp(rectLo, na, Comp(p1, na)); SetComp(rectHi, na, Comp(p1, na));
				SetComp(rectLo, u, std::min(Comp(p1, u), Comp(q, u)));
				SetComp(rectHi, u, std::max(Comp(p1, u), Comp(q, u)));
				SetComp(rectLo, v, std::min(Comp(p1, v), Comp(q, v)));
				SetComp(rectHi, v, std::max(Comp(p1, v), Comp(q, v)));
				stage = 2;
				ed.SetStatus("Rect: pick the depth  (RMB to cut)");
			} else {
				// The final click's button decides the action: LMB = apply, RMB = alt.
				std::vector<IntVector3> cells;
				Cells(q, cells);
				if (rmb)
					applyAlt(ed, cells);
				else
					apply(ed, cells);
				Reset();
				ed.SetStatus(rmb ? "Rect cut" : "Rect applied");
			}
		}

		bool RectSubTool::OnEscape(IEditorContext& ed) {
			if (stage == 0)
				return false;
			Reset();
			ed.SetStatus("Rect cancelled");
			return true;
		}

		void RectSubTool::DrawScene(IEditorContext& ed) {
			if (stage == 0) {
				ed.DoPick();
				if (ed.HasPick()) {
					IntVector3 h = ed.PickSolid();
					ed.DrawCellOutline(h.x, h.y, h.z, kHover);
				}
				return;
			}
			IntVector3 q;
			if (!ShapeCur(ed, q)) {
				ed.DrawCellOutline(p1.x, p1.y, p1.z, kHover);
				return;
			}
			IntVector3 lo, hi;
			BBox(q, lo, hi);
			if (useMirror)
				ed.DrawBoxOutlineMirrored(lo, hi, kHover);
			else
				ed.DrawBoxOutline(lo, hi, kHover);
		}

		// --- MoveSubTool (drag a 3-axis gizmo) -------------------------------

		void MoveSubTool::OnActivate(IEditorContext& ed) {
			grabAxis = -1;
			ed.SetStatus("Move: drag an axis handle to move the selection");
		}

		int MoveSubTool::OffsetAlong(IEditorContext& ed, const Vector3& c, int axis) const {
			bool ok1, ok2;
			Vector2 s0 = ed.WorldToScreen(c, ok1);
			Vector2 sa = ed.WorldToScreen(c + AxisUnit(axis), ok2); // 1 voxel along axis
			if (!ok1 || !ok2)
				return 0;
			Vector2 da = sa - s0;
			float dl = da.GetLength();
			if (dl < 0.5F)
				return 0; // axis ~parallel to the view
			Vector2 m = ed.CursorPos() - grabCursor;
			return int(std::lround(Vector2::Dot(m, da) / (dl * dl)));
		}

		void MoveSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (!e.IsLeft())
				return;
			if (e.IsDown()) {
				Vector3 c;
				if (!ed.SelectionCentroid(c))
					return;
				Vector2 cur = ed.CursorPos();
				int best = -1;
				float bestDist = 12.0F; // pixels
				for (int a = 0; a < 3; a++) {
					bool ok1, ok2;
					Vector2 s0 = ed.WorldToScreen(c, ok1);
					Vector2 sa = ed.WorldToScreen(c + AxisUnit(a) * kGizLen, ok2);
					if (!ok1 || !ok2)
						continue;
					float d = DistToSeg(cur, s0, sa);
					if (d < bestDist) { bestDist = d; best = a; }
				}
				if (best >= 0) { grabAxis = best; grabCursor = cur; curOffset = 0; }
			} else if (e.IsUp()) {
				if (grabAxis < 0)
					return;
				Vector3 c;
				int off = ed.SelectionCentroid(c) ? OffsetAlong(ed, c, grabAxis) : 0;
				if (off != 0) {
					int d[3] = {0, 0, 0};
					d[grabAxis] = off;
					ed.MoveSelection(d[0], d[1], d[2]);
				}
				grabAxis = -1;
			}
		}

		bool MoveSubTool::OnEscape(IEditorContext&) {
			if (grabAxis < 0)
				return false;
			grabAxis = -1; // cancel the drag (no move committed)
			return true;
		}

		void MoveSubTool::DrawScene(IEditorContext& ed) {
			Vector3 c;
			if (!ed.SelectionCentroid(c))
				return;
			curOffset = (grabAxis >= 0) ? OffsetAlong(ed, c, grabAxis) : 0;
			for (int a = 0; a < 3; a++) {
				Vector4 col = (grabAxis == a) ? MakeVector4(1, 1, 1, 1) : kAxisCol[a];
				ed.DrawLine3D(c, c + AxisUnit(a) * kGizLen, col);
			}
			if (grabAxis >= 0 && curOffset != 0) {
				int d[3] = {0, 0, 0};
				d[grabAxis] = curOffset;
				ed.DrawSelectionOffset(d[0], d[1], d[2], MakeVector4(0.4F, 1.0F, 0.5F, 0.9F));
			}
		}
	} // namespace gui
} // namespace spades
