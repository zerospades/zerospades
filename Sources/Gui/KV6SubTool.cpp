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
			const float kGizLen = 12.0F;   // gizmo handle length (voxels)
			const float kCube = 0.8F;      // handle cube half-size
			const float kCubeHi = 1.05F;   // handle cube half-size when active

			// Distance from point p to the segment [a, b] in screen space.
			float DistToSeg(const Vector2& p, const Vector2& a, const Vector2& b) {
				Vector2 ab = b - a;
				float l2 = Vector2::Dot(ab, ab);
				float t = (l2 < 1.0e-6F) ? 0.0F : Vector2::Dot(p - a, ab) / l2;
				t = std::max(0.0F, std::min(1.0F, t));
				return (p - (a + ab * t)).GetLength();
			}

			// Wireframe cube of half-size h centred at c (the draggable axis handle).
			void DrawCube(IEditorContext& ed, const Vector3& c, float h, const Vector4& col) {
				Vector3 p[8];
				for (int i = 0; i < 8; i++)
					p[i] = c + MakeVector3((i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h);
				static const int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
				                             {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
				for (int i = 0; i < 12; i++)
					ed.DrawLine3D(p[e[i][0]], p[e[i][1]], col);
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
			ed.SetStatus(rmb ? "Rect cut" : "Rect applied");
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

		int MoveSubTool::HitAxis(IEditorContext& ed, const Vector3& c) const {
			bool ok0;
			Vector2 s0 = ed.WorldToScreen(c, ok0);
			if (!ok0)
				return -1;
			Vector2 cur = ed.CursorPos();
			int best = -1;
			float bestDist = 14.0F; // pixels
			for (int a = 0; a < 3; a++) {
				bool ok1;
				Vector2 tip = ed.WorldToScreen(c + AxisUnit(a) * kGizLen, ok1);
				if (!ok1)
					continue;
				// Along the axis line, or near the tip cube (a bigger, easier target).
				float d = std::min(DistToSeg(cur, s0, tip), (cur - tip).GetLength());
				if (d < bestDist) { bestDist = d; best = a; }
			}
			return best;
		}

		void MoveSubTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (!e.IsLeft())
				return;
			if (e.IsDown()) {
				Vector3 c;
				if (!ed.SelectionCentroid(c))
					return;
				int best = HitAxis(ed, c);
				if (best >= 0) { grabAxis = best; grabCursor = ed.CursorPos(); curOffset = 0; }
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
			int hover = (grabAxis < 0) ? HitAxis(ed, c) : -1;
			for (int a = 0; a < 3; a++) {
				bool active = (grabAxis == a) || (hover == a);
				Vector4 col = active ? MakeVector4(1, 1, 1, 1) : kAxisCol[a];
				Vector3 tip = c + AxisUnit(a) * kGizLen;
				ed.DrawLine3D(c, tip, col);
				DrawCube(ed, tip, active ? kCubeHi : kCube, col); // draggable handle
			}
			if (grabAxis >= 0 && curOffset != 0) {
				int d[3] = {0, 0, 0};
				d[grabAxis] = curOffset;
				ed.DrawSelectionOffset(d[0], d[1], d[2], MakeVector4(0.4F, 1.0F, 0.5F, 0.9F));
			}
		}
	} // namespace gui
} // namespace spades
