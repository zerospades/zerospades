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

namespace spades {

	// A 3-click circular cylinder, authored in AngelScript as a demo of editor tool
	// scripting. Click the centre on a voxel face, click again to set the radius
	// (the clicked cell lies on the rim), then click the depth along the clicked
	// face's normal. mode 0 = Draw (fill / erase), mode 1 = Select (select /
	// deselect). The same class serves both containers.
	class CylinderTool : EditorTool {
		private int mode;
		private int stage = 0; // 0 none, 1 centre set, 2 radius set
		private int normalAxis = 2;
		private IntVector3 p0; // centre
		private IntVector3 p1; // rim point (sets the radius)
		private IntVector3 cur; // point under the cursor for the current stage

		CylinderTool(int m) { mode = m; }

		string Label() { return "Cylinder"; }

		void OnActivate(EditorContext@ ctx) {
			stage = 0;
			ctx.SetStatus("Cylinder: click the centre on a voxel face");
		}
		void OnDeactivate(EditorContext@ ctx) { stage = 0; }
		void OnKey(EditorContext@ ctx, string key, bool down) {}

		bool OnEscape(EditorContext@ ctx) {
			if (stage == 0)
				return false;
			stage = 0;
			ctx.SetStatus("Cylinder cancelled");
			return true;
		}

		void OnPointer(EditorContext@ ctx, int button, int phase, bool alt, bool ctrl, bool shift) {
			if (phase != EditorPhase::PhaseDown)
				return;
			bool lmb = (button == EditorButton::ButtonLeft);
			bool rmb = (button == EditorButton::ButtonRight);
			if (!lmb && !rmb)
				return;

			// First click: anchor the centre on a solid face and capture the normal.
			if (stage == 0) {
				ctx.DoPick();
				if (!ctx.HasPick())
					return;
				p0 = ctx.PickSolid();
				IntVector3 place = ctx.PickPlace();
				int dx = place.x - p0.x;
				int dy = place.y - p0.y;
				normalAxis = (dx != 0) ? 0 : ((dy != 0) ? 1 : 2);
				stage = 1;
				ctx.SetStatus("Cylinder: click to set the radius");
				return;
			}

			if (!StagePoint(ctx))
				return;
			if (stage == 1) { // fix the radius, then pick the depth
				p1 = cur;
				stage = 2;
				ctx.SetStatus("Cylinder: pick the depth  (RMB to cut)");
				return;
			}

			// Final click: build the cylinder and apply (button decides fill vs cut).
			array<IntVector3>@ cells = BuildCylinder(cur);
			Apply(ctx, cells, rmb);
			stage = 0;
			ctx.SetStatus(rmb ? "Cylinder cut" : "Cylinder applied");
		}

		void DrawScene(EditorContext@ ctx) {
			Vector4 hover = Vector4(0.3f, 0.8f, 1.0f, 0.95f);
			if (stage == 0) {
				ctx.DoPick();
				if (ctx.HasPick()) {
					IntVector3 h = ctx.PickSolid();
					ctx.DrawCellOutline(h.x, h.y, h.z, hover);
				}
				return;
			}
			if (!StagePoint(ctx)) {
				ctx.DrawCellOutline(p0.x, p0.y, p0.z, hover);
				return;
			}
			// Simple bounding-box preview for now (a proper cylinder rim preview is
			// left for later).
			int na = normalAxis;
			int u = (na + 1) % 3;
			int v = (na + 2) % 3;
			int radSq = (stage == 1) ? RadiusSq(cur) : RadiusSq(p1);
			int R = 0;
			while ((R + 1) * (R + 1) <= radSq) R++; // R = floor(radius)
			int uc = Comp(p0, u);
			int vc = Comp(p0, v);
			int nA = Comp(p0, na);
			int nB = (stage == 1) ? Comp(p0, na) : Comp(cur, na);
			IntVector3 lo = BuildCell(na, IMin(nA, nB), u, uc - R, v, vc - R);
			IntVector3 hi = BuildCell(na, IMax(nA, nB), u, uc + R, v, vc + R);
			ctx.DrawBoxOutlineMirrored(lo, hi, hover);
		}

		// --- helpers -----------------------------------------------------------

		private int Comp(IntVector3 vec, int axis) {
			if (axis == 0) return vec.x;
			if (axis == 1) return vec.y;
			return vec.z;
		}
		private Vector3 AxisVec(int axis) {
			if (axis == 0) return Vector3(1.0f, 0.0f, 0.0f);
			if (axis == 1) return Vector3(0.0f, 1.0f, 0.0f);
			return Vector3(0.0f, 0.0f, 1.0f);
		}
		private int IMin(int a, int b) { return a < b ? a : b; }
		private int IMax(int a, int b) { return a > b ? a : b; }

		// na/u/v are a permutation of {0,1,2}; build a cell with each component set.
		private IntVector3 BuildCell(int na, int nVal, int u, int uVal, int v, int vVal) {
			int xs = 0, ys = 0, zs = 0;
			if (na == 0) xs = nVal; else if (na == 1) ys = nVal; else zs = nVal;
			if (u == 0) xs = uVal; else if (u == 1) ys = uVal; else zs = uVal;
			if (v == 0) xs = vVal; else if (v == 1) ys = vVal; else zs = vVal;
			return IntVector3(xs, ys, zs);
		}

		// Squared radius from the centre to a rim point, in the face plane.
		private int RadiusSq(IntVector3 rim) {
			int na = normalAxis;
			int u = (na + 1) % 3;
			int v = (na + 2) % 3;
			int du = Comp(rim, u) - Comp(p0, u);
			int dv = Comp(rim, v) - Comp(p0, v);
			return du * du + dv * dv;
		}

		// Resolve `cur` for the current stage: the rim point on the face plane
		// (stage 1) or the depth along the normal (stage 2). Returns false if the
		// cursor ray doesn't meet the plane.
		private bool StagePoint(EditorContext@ ctx) {
			Vector3 pp = Vector3(p0);
			if (stage == 1)
				return ctx.RayPlaneCell(pp, AxisVec(normalAxis), cur);
			IntVector3 qd;
			if (!ctx.RayPlaneCell(pp, ctx.ViewDir(), qd))
				return false;
			cur = p0; // depth point = centre with its normal-axis component replaced
			if (normalAxis == 0) cur.x = qd.x;
			else if (normalAxis == 1) cur.y = qd.y;
			else cur.z = qd.z;
			return true;
		}

		// Voxels of the filled circular cylinder: a disc of squared-radius RadiusSq
		// around the centre, extruded along the face normal from the centre to
		// `depth`.
		private array<IntVector3>@ BuildCylinder(IntVector3 depth) {
			int na = normalAxis;
			int u = (na + 1) % 3;
			int v = (na + 2) % 3;
			int uc = Comp(p0, u);
			int vc = Comp(p0, v);
			int radSq = RadiusSq(p1);
			int r = 0;
			while (r * r <= radSq) r++;
			int nmin = IMin(Comp(p0, na), Comp(depth, na));
			int nmax = IMax(Comp(p0, na), Comp(depth, na));

			array<IntVector3>@ cells = array<IntVector3>();
			for (int n = nmin; n <= nmax; n++) {
				for (int du = -r; du <= r; du++) {
					int du2 = du * du;
					for (int dv = -r; dv <= r; dv++) {
						if (du2 + dv * dv <= radSq)
							cells.insertLast(BuildCell(na, n, u, uc + du, v, vc + dv));
					}
				}
			}
			return cells;
		}

		private void Apply(EditorContext@ ctx, array<IntVector3>@ cells, bool alt) {
			if (mode == 0) { // Draw
				if (alt) ctx.EraseCells(cells);
				else ctx.FillCells(cells, ctx.CurrentColor());
			} else { // Select
				if (alt) ctx.DeselectCells(cells);
				else ctx.SelectCells(cells);
			}
		}
	}

	// Factory invoked by the C++ SubToolRegistry (one instance per container).
	EditorTool@ CreateCylinderTool(int mode) {
		return CylinderTool(mode);
	}

}
