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
#include "KV6EditorView.h"

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
			const Vector4 kHover = MakeVector4(0.3F, 0.8F, 1.0F, 0.95F);
			const Vector4 kSelected = MakeVector4(1.0F, 0.3F, 0.3F, 0.95F);
			const Vector4 kTarget = MakeVector4(1.0F, 0.9F, 0.3F, 0.9F);
		} // namespace

		// --- BlockSubTool (Draw single) --------------------------------------

		void BlockSubTool::OnActivate(KV6EditorView& ed) {
			ed.SetStatus("Draw: LMB place  -  RMB delete  -  Alt+LMB pick colour");
		}
		void BlockSubTool::OnPointerDown(KV6EditorView& ed, const std::string& button) {
			if (button == "LeftMouseButton") {
				if (ed.AltHeld() || ed.PickModeActive()) {
					ed.Eyedropper();
					ed.ClearPickMode();
					return;
				}
				ed.PlaceCube();
			} else if (button == "RightMouseButton") {
				ed.DeleteCube();
			}
		}
		void BlockSubTool::DrawScene(KV6EditorView& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 p = ed.PickPlace(), h = ed.PickSolid();
			ed.DrawCellOutline(p.x, p.y, p.z, ed.ColorToVec(ed.CurrentColor()));
			ed.DrawCellOutline(h.x, h.y, h.z, kTarget);
		}

		// --- PointSubTool (Select single) ------------------------------------

		void PointSubTool::OnActivate(KV6EditorView& ed) {
			ed.SetStatus("Select Point: click to (de)select  -  click empty to clear");
		}
		void PointSubTool::OnPointerDown(KV6EditorView& ed, const std::string& button) {
			if (button != "LeftMouseButton")
				return;
			ed.DoPick();
			if (ed.HasPick()) {
				IntVector3 h = ed.PickSolid();
				ed.ToggleSelect(h.x, h.y, h.z);
			} else {
				ed.ClearSelection();
			}
		}
		void PointSubTool::DrawScene(KV6EditorView& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 h = ed.PickSolid();
			ed.DrawCellOutline(h.x, h.y, h.z, ed.IsSelected(h.x, h.y, h.z) ? kSelected : kHover);
		}

		// --- ByColourSubTool (Select flood-fill) -----------------------------

		void ByColourSubTool::OnActivate(KV6EditorView& ed) {
			ed.SetStatus("Select By Colour: click a voxel to select its colour region  -  [L]");
		}
		void ByColourSubTool::OnPointerDown(KV6EditorView& ed, const std::string& button) {
			if (button != "LeftMouseButton")
				return;
			ed.DoPick();
			if (ed.HasPick()) {
				IntVector3 h = ed.PickSolid();
				ed.SelectLinkedColor(h.x, h.y, h.z);
			} else {
				ed.ClearSelection();
			}
		}
		void ByColourSubTool::OnKey(KV6EditorView& ed, const std::string& key, bool down) {
			if (down && EqualsIgnoringCase(key, "L")) {
				ed.DoPick();
				if (ed.HasPick()) {
					IntVector3 h = ed.PickSolid();
					ed.SelectLinkedColor(h.x, h.y, h.z);
				}
			}
		}
		void ByColourSubTool::DrawScene(KV6EditorView& ed) {
			ed.DoPick();
			if (!ed.HasPick())
				return;
			IntVector3 h = ed.PickSolid();
			ed.DrawCellOutline(h.x, h.y, h.z, kHover);
		}

		// --- ShapeSubTool (Rect / Cylinder, shared) --------------------------

		void ShapeSubTool::OnActivate(KV6EditorView& ed) {
			Reset();
			ed.SetStatus(kind == Rect ? "Rect: pick a corner" : "Cylinder: pick the centre");
		}

		int ShapeSubTool::RadiusTo(const IntVector3& cur) const {
			int u = (normalAxis + 1) % 3, v = (normalAxis + 2) % 3;
			int du = Comp(cur, u) - Comp(p1, u), dv = Comp(cur, v) - Comp(p1, v);
			return int(std::lround(std::sqrt(double(du * du + dv * dv))));
		}

		void ShapeSubTool::BBox(const IntVector3& cur, IntVector3& lo, IntVector3& hi) const {
			int na = normalAxis, u = (na + 1) % 3, v = (na + 2) % 3;
			if (kind == Rect) {
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
			} else {
				int cu = Comp(p1, u), cv = Comp(p1, v);
				int r = (stage >= 2) ? radius : RadiusTo(cur);
				SetComp(lo, u, cu - r); SetComp(hi, u, cu + r);
				SetComp(lo, v, cv - r); SetComp(hi, v, cv + r);
				if (stage == 1) { SetComp(lo, na, Comp(p1, na)); SetComp(hi, na, Comp(p1, na)); }
				else {
					SetComp(lo, na, std::min(Comp(p1, na), Comp(cur, na)));
					SetComp(hi, na, std::max(Comp(p1, na), Comp(cur, na)));
				}
			}
		}

		void ShapeSubTool::Cells(const IntVector3& cur, std::vector<IntVector3>& out) const {
			IntVector3 lo, hi;
			BBox(cur, lo, hi);
			out.clear();
			if (kind == Rect) {
				for (int x = lo.x; x <= hi.x; x++)
				for (int y = lo.y; y <= hi.y; y++)
				for (int z = lo.z; z <= hi.z; z++)
					out.push_back(MakeIntVector3(x, y, z));
			} else {
				int na = normalAxis, u = (na + 1) % 3, v = (na + 2) % 3;
				int cu = Comp(p1, u), cv = Comp(p1, v);
				int r = (stage >= 2) ? radius : RadiusTo(cur);
				for (int x = lo.x; x <= hi.x; x++)
				for (int y = lo.y; y <= hi.y; y++)
				for (int z = lo.z; z <= hi.z; z++) {
					IntVector3 c = MakeIntVector3(x, y, z);
					int du = Comp(c, u) - cu, dv = Comp(c, v) - cv;
					if (du * du + dv * dv <= r * r)
						out.push_back(c);
				}
			}
		}

		void ShapeSubTool::OnPointerDown(KV6EditorView& ed, const std::string& button) {
			if (button == "RightMouseButton") { Reset(); return; } // cancel
			if (button != "LeftMouseButton")
				return;
			ed.DoPick();
			if (!ed.HasPick()) { Reset(); return; }
			IntVector3 h = ed.PickSolid();
			if (stage == 0) {
				p1 = h;
				IntVector3 d = ed.PickPlace() - h;
				normalAxis = (d.x != 0) ? 0 : (d.y != 0) ? 1 : 2;
				stage = 1;
				ed.SetStatus(kind == Rect ? "Rect: pick the opposite corner"
				                          : "Cylinder: pick the radius");
			} else if (stage == 1) {
				if (kind == Rect) {
					int na = normalAxis, u = (na + 1) % 3, v = (na + 2) % 3;
					SetComp(rectLo, na, Comp(p1, na)); SetComp(rectHi, na, Comp(p1, na));
					SetComp(rectLo, u, std::min(Comp(p1, u), Comp(h, u)));
					SetComp(rectHi, u, std::max(Comp(p1, u), Comp(h, u)));
					SetComp(rectLo, v, std::min(Comp(p1, v), Comp(h, v)));
					SetComp(rectHi, v, std::max(Comp(p1, v), Comp(h, v)));
				} else {
					radius = RadiusTo(h);
				}
				stage = 2;
				ed.SetStatus("Pick the depth");
			} else {
				std::vector<IntVector3> cells;
				Cells(h, cells);
				apply(ed, cells);
				Reset();
				ed.SetStatus(kind == Rect ? "Rect applied" : "Cylinder applied");
			}
		}

		void ShapeSubTool::DrawScene(KV6EditorView& ed) {
			ed.DoPick();
			if (stage == 0) {
				if (ed.HasPick()) {
					IntVector3 h = ed.PickSolid();
					ed.DrawCellOutline(h.x, h.y, h.z, kHover);
				}
				return;
			}
			if (ed.HasPick()) {
				IntVector3 h = ed.PickSolid(), lo, hi;
				BBox(h, lo, hi);
				ed.DrawBoxOutline(lo, hi, kHover);
			} else {
				ed.DrawCellOutline(p1.x, p1.y, p1.z, kHover);
			}
		}
	} // namespace gui
} // namespace spades
