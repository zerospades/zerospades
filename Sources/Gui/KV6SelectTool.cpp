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
			const char* kShapeNames[] = {"Block", "Rect"};
			const float kBx = 12.0F, kBw = 64.0F, kBh = 22.0F, kBgap = 4.0F;

			int Comp(const IntVector3& v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }
			void SetComp(IntVector3& v, int a, int val) {
				if (a == 0) v.x = val;
				else if (a == 1) v.y = val;
				else v.z = val;
			}
			Vector4 kHover = MakeVector4(0.3F, 0.8F, 1.0F, 0.95F);
		} // namespace

		void SelectTool::ResetRect() { stage = 0; }

		void SelectTool::OnActivate(KV6EditorView& ed) {
			ResetRect();
			ed.SetStatus("Select: click to (de)select  -  [L] linked colour  -  pick a shape");
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

		bool SelectTool::ShapeButtonHit(KV6EditorView& ed, int& outShape) const {
			Vector2 c = ed.CursorPos();
			float y = ed.ViewportTop() + 6.0F;
			for (int i = 0; i < ShapeCount; i++) {
				float x = kBx + float(i) * (kBw + kBgap);
				if (c.x >= x && c.x < x + kBw && c.y >= y && c.y < y + kBh) {
					outShape = i;
					return true;
				}
			}
			return false;
		}

		void SelectTool::OnPointerDown(KV6EditorView& ed, const std::string& button) {
			if (button != "LeftMouseButton")
				return;
			int s;
			if (ShapeButtonHit(ed, s)) {
				if (s != shape) { shape = s; ResetRect(); }
				return;
			}

			ed.DoPick();
			if (shape == Block) {
				if (ed.HasPick()) {
					IntVector3 h = ed.PickSolid();
					ed.ToggleSelect(h.x, h.y, h.z);
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
			if (shape == Block) {
				if (!ed.HasPick())
					return;
				IntVector3 h = ed.PickSolid();
				Vector4 col = ed.IsSelected(h.x, h.y, h.z) ? MakeVector4(1.0F, 0.3F, 0.3F, 0.95F)
				                                           : kHover;
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

		void SelectTool::DrawOverlay(KV6EditorView& ed) {
			float y = ed.ViewportTop() + 6.0F;
			for (int i = 0; i < ShapeCount; i++) {
				float x = kBx + float(i) * (kBw + kBgap);
				bool on = shape == i;
				ed.Fill2D(x, y, kBw, kBh, on ? MakeVector4(0.22F, 0.45F, 0.70F, 1.0F)
				                             : MakeVector4(0.14F, 0.14F, 0.16F, 0.9F));
				ed.Stroke2D(x, y, kBw, kBh, 1.0F, MakeVector4(0.5F, 0.5F, 0.5F, 0.5F));
				Vector2 ts = ed.MeasureText(kShapeNames[i]);
				ed.Text2D(kShapeNames[i], x + (kBw - ts.x * 0.8F) * 0.5F,
				          y + (kBh - ts.y * 0.8F) * 0.5F, 0.8F, MakeVector4(1, 1, 1, 1));
			}
		}
	} // namespace gui
} // namespace spades
