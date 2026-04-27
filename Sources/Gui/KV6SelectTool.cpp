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

#include "KV6SelectTool.h"
#include "KV6EditorView.h"

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
			Vector4 kHover = MakeVector4(0.3F, 0.8F, 1.0F, 0.95F);
		} // namespace

		void SelectTool::ResetRect() { stage = 0; }

		const char* SelectTool::SubToolLabel(int i) const {
			return i == Point ? "Point" : (i == Rect ? "Rect" : "By Colour");
		}
		void SelectTool::SetSubTool(int i) {
			if (i != sub) { sub = i; ResetRect(); }
		}

		void SelectTool::OnActivate(KV6EditorView& ed) {
			ResetRect();
			ed.SetStatus("Select: pick a sub-tool  -  [L] flood by colour  -  click empty to clear");
		}

		void SelectTool::RectBox(const IntVector3& cur, IntVector3& lo, IntVector3& hi) const {
			for (int a = 0; a < 3; a++) {
				int v1 = Comp(p1, a), vc = Comp(cur, a);
				if (stage == 1) {
					// Rectangle in the clicked face's plane (fixed along the normal).
					if (a == normalAxis) { SetComp(lo, a, v1); SetComp(hi, a, v1); }
					else { SetComp(lo, a, std::min(v1, vc)); SetComp(hi, a, std::max(v1, vc)); }
				} else {
					// Rectangle extruded along the normal up to the depth point.
					if (a == normalAxis) {
						SetComp(lo, a, std::min(v1, vc)); SetComp(hi, a, std::max(v1, vc));
					} else {
						SetComp(lo, a, Comp(rectLo, a)); SetComp(hi, a, Comp(rectHi, a));
					}
				}
			}
		}

		void SelectTool::OnPointerDown(KV6EditorView& ed, const std::string& button) {
			if (button != "LeftMouseButton")
				return;
			ed.DoPick();

			if (sub == Point) {
				if (ed.HasPick()) {
					IntVector3 h = ed.PickSolid();
					ed.ToggleSelect(h.x, h.y, h.z);
				} else {
					ed.ClearSelection();
				}
				return;
			}

			if (sub == ByColour) {
				if (ed.HasPick()) {
					IntVector3 h = ed.PickSolid();
					ed.SelectLinkedColor(h.x, h.y, h.z);
				} else {
					ed.ClearSelection();
				}
				return;
			}

			// Rect: corner -> opposite corner -> depth.
			if (!ed.HasPick()) { ResetRect(); return; }
			IntVector3 h = ed.PickSolid();
			if (stage == 0) {
				p1 = h;
				IntVector3 d = ed.PickPlace() - h; // unit step toward the clicked face
				normalAxis = (d.x != 0) ? 0 : (d.y != 0) ? 1 : 2;
				stage = 1;
				ed.SetStatus("Rect: pick the opposite corner");
			} else if (stage == 1) {
				RectBox(h, rectLo, rectHi);
				stage = 2;
				ed.SetStatus("Rect: pick the depth");
			} else {
				IntVector3 lo, hi;
				RectBox(h, lo, hi);
				ed.SelectBox(lo, hi);
				ResetRect();
				ed.SetStatus("Rect selected");
			}
		}

		void SelectTool::OnKey(KV6EditorView& ed, const std::string& key, bool down) {
			if (down && EqualsIgnoringCase(key, "L")) {
				ed.DoPick();
				if (ed.HasPick()) {
					IntVector3 h = ed.PickSolid();
					ed.SelectLinkedColor(h.x, h.y, h.z);
				}
			}
		}

		void SelectTool::DrawScene(KV6EditorView& ed) {
			ed.DoPick();
			if (sub == Point || sub == ByColour) {
				if (!ed.HasPick())
					return;
				IntVector3 h = ed.PickSolid();
				Vector4 col = (sub == Point && ed.IsSelected(h.x, h.y, h.z))
				                ? MakeVector4(1.0F, 0.3F, 0.3F, 0.95F) : kHover;
				ed.DrawCellOutline(h.x, h.y, h.z, col);
				return;
			}
			// Rect preview.
			if (stage == 0) {
				if (ed.HasPick()) {
					IntVector3 h = ed.PickSolid();
					ed.DrawCellOutline(h.x, h.y, h.z, kHover);
				}
				return;
			}
			if (ed.HasPick()) {
				IntVector3 h = ed.PickSolid(), lo, hi;
				RectBox(h, lo, hi);
				ed.DrawBoxOutline(lo, hi, kHover);
			} else {
				ed.DrawCellOutline(p1.x, p1.y, p1.z, kHover);
			}
		}
	} // namespace gui
} // namespace spades
