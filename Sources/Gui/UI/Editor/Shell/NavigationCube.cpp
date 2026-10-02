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

#include "NavigationCube.h"

#include <cmath>
#include <cstddef>
#include <vector>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>

namespace spades {
	namespace gui {
		namespace {
			Vector3 AxisUnit3(int a) {
				return MakeVector3(a == 0 ? 1.0F : 0.0F, a == 1 ? 1.0F : 0.0F, a == 2 ? 1.0F : 0.0F);
			}
			Vector4 AxisTint(int a) {
				if (a == 0) return MakeVector4(0.78F, 0.5F, 0.5F, 1.0F);
				if (a == 1) return MakeVector4(0.5F, 0.74F, 0.5F, 1.0F);
				return MakeVector4(0.55F, 0.58F, 0.78F, 1.0F);
			}
			const char* FaceLabel(int ax, int sg) {
				static const char* names[3][2] = {
				  {"Right", "Left"}, {"Back", "Front"}, {"Top", "Bottom"}};
				return names[ax][sg > 0 ? 1 : 0];
			}
			// Inside a convex polygon (n verts, ordered): consistent edge-cross signs.
			bool PointInPoly(const Vector2& p, const Vector2* q, int n) {
				float sign = 0.0F;
				for (int i = 0; i < n; i++) {
					const Vector2& a = q[i];
					const Vector2& b = q[(i + 1) % n];
					float cr = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
					if (i == 0) sign = cr;
					else if (cr * sign < 0.0F) return false;
				}
				return true;
			}

			// One facet of the chamfered (beveled) navigation cube.
			struct NaviFacet {
				int n;             // 3 (corner bevel) or 4 (face / edge bevel)
				Vector3 v[4];      // world-space vertices
				Vector3 dir;       // outward normal = snap view direction
				int tint;          // axis 0/1/2 for faces, -1 for bevels
				const char* label; // for the 6 faces, else nullptr
			};

			Vector3 Comp3(int ax, float on, int u, float uu, int v, float vv) {
				float c[3];
				c[ax] = on; c[u] = uu; c[v] = vv;
				return MakeVector3(c[0], c[1], c[2]);
			}

			// Build the 6 faces + 12 edge bevels + 8 corner bevels of the cube.
			void BuildNaviFacets(std::vector<NaviFacet>& out) {
				const float t = 0.66F; // face half-extent; bevels live in [t, 1]
				out.clear();
				// 6 faces (square, shrunk to +/-t).
				for (int ax = 0; ax < 3; ax++) {
					int u = (ax + 1) % 3, v = (ax + 2) % 3;
					for (int s = -1; s <= 1; s += 2) {
						NaviFacet f;
						f.n = 4;
						f.v[0] = Comp3(ax, float(s), u, -t, v, -t);
						f.v[1] = Comp3(ax, float(s), u, t, v, -t);
						f.v[2] = Comp3(ax, float(s), u, t, v, t);
						f.v[3] = Comp3(ax, float(s), u, -t, v, t);
						f.dir = AxisUnit3(ax) * float(s);
						f.tint = ax;
						f.label = FaceLabel(ax, s);
						out.push_back(f);
					}
				}
				// 12 edge bevels (one per axis-pair and sign pair).
				const int pairs[3][2] = {{0, 1}, {1, 2}, {2, 0}};
				for (int pi = 0; pi < 3; pi++) {
					int a = pairs[pi][0], b = pairs[pi][1], c = 3 - a - b;
					for (int sa = -1; sa <= 1; sa += 2)
					for (int sb = -1; sb <= 1; sb += 2) {
						NaviFacet f;
						f.n = 4;
						float c0[3], c1[3], c2[3], c3[3];
						c0[a] = float(sa); c0[b] = sb * t; c0[c] = -t;
						c1[a] = float(sa); c1[b] = sb * t; c1[c] = t;
						c2[a] = sa * t; c2[b] = float(sb); c2[c] = t;
						c3[a] = sa * t; c3[b] = float(sb); c3[c] = -t;
						f.v[0] = MakeVector3(c0[0], c0[1], c0[2]);
						f.v[1] = MakeVector3(c1[0], c1[1], c1[2]);
						f.v[2] = MakeVector3(c2[0], c2[1], c2[2]);
						f.v[3] = MakeVector3(c3[0], c3[1], c3[2]);
						f.dir = (AxisUnit3(a) * float(sa) + AxisUnit3(b) * float(sb)).Normalize();
						f.tint = -1;
						f.label = nullptr;
						out.push_back(f);
					}
				}
				// 8 corner bevels (triangles).
				for (int sx = -1; sx <= 1; sx += 2)
				for (int sy = -1; sy <= 1; sy += 2)
				for (int sz = -1; sz <= 1; sz += 2) {
					NaviFacet f;
					f.n = 3;
					f.v[0] = MakeVector3(sx * 1.0F, sy * t, sz * t);
					f.v[1] = MakeVector3(sx * t, sy * 1.0F, sz * t);
					f.v[2] = MakeVector3(sx * t, sy * t, sz * 1.0F);
					f.dir = MakeVector3(float(sx), float(sy), float(sz)).Normalize();
					f.tint = -1;
					f.label = nullptr;
					out.push_back(f);
				}
			}

			// The navigation cube is fixed geometry, so build the facet set once and
			// hand back the shared copy (queried every frame for drawing and picking).
			const std::vector<NaviFacet>& NaviFacets() {
				static const std::vector<NaviFacet> facets = [] {
					std::vector<NaviFacet> out;
					BuildNaviFacets(out);
					return out;
				}();
				return facets;
			}
			// Facets turned further away than this are not drawn or picked.
			constexpr float kFacingThreshold = 0.02F;

			// The home button: its side in pixels, and its house glyph's half
			// width as a fraction of that side.
			constexpr float kHomeSize = 22.0F;
			constexpr float kHomeGlyph = 0.3F;
			const Vector4 kHighlight = MakeVector4(0.4F, 0.7F, 1.0F, 0.97F);
		} // namespace

		void NavigationCube::Layout(float centreX, float centreY, float radius) {
			cx = centreX;
			cy = centreY;
			r = radius;
		}

		Vector2 NavigationCube::Project(const GizmoView& view, const Vector3& v) const {
			return MakeVector2(cx + Vector3::Dot(v, view.right) * r,
			                   cy - Vector3::Dot(v, view.up) * r);
		}

		Vector2 NavigationCube::HomeCentre() const {
			// Beside the cube's top left, clear of the cube in every view but the
			// corners it reaches looking along a diagonal.
			return MakeVector2(cx - r - kHomeSize * 0.5F, cy - r + kHomeSize * 0.5F);
		}

		bool NavigationCube::HomeAt(const Vector2& p) const {
			const Vector2 c = HomeCentre();
			const float h = kHomeSize * 0.5F;
			return std::fabs(p.x - c.x) <= h && std::fabs(p.y - c.y) <= h;
		}

		void NavigationCube::DrawHome(client::IRenderer& renderer, bool hovered) const {
			const Vector2 c = HomeCentre();
			const float h = kHomeSize * 0.5F;
			const Vector2 box[4] = {c + MakeVector2(-h, -h), c + MakeVector2(h, -h),
			                        c + MakeVector2(h, h), c + MakeVector2(-h, h)};
			OverlayFillConvexPolygon(renderer, box, 4,
			                         hovered ? kHighlight : MakeVector4(0.2F, 0.21F, 0.24F, 0.92F));
			const Vector4 ec = MakeVector4(0.08F, 0.08F, 0.1F, 0.85F);
			for (int e = 0; e < 4; e++)
				OverlayStrokeLine(renderer, box[e], box[(e + 1) % 4], 1.0F, ec);

			// A house: a roof over a square body.
			const float g = kHomeSize * kHomeGlyph;
			const Vector4 ink = MakeVector4(0.92F, 0.92F, 0.95F, 1.0F);
			const Vector2 roof[3] = {c + MakeVector2(0.0F, -g), c + MakeVector2(g, -g * 0.1F),
			                         c + MakeVector2(-g, -g * 0.1F)};
			OverlayFillConvexPolygon(renderer, roof, 3, ink);
			const float b = g * 0.65F;
			const Vector2 body[4] = {c + MakeVector2(-b, -g * 0.1F), c + MakeVector2(b, -g * 0.1F),
			                         c + MakeVector2(b, g), c + MakeVector2(-b, g)};
			OverlayFillConvexPolygon(renderer, body, 4, ink);
		}

		bool NavigationCube::DirectionAt(const GizmoView& view, const Vector2& p,
		                                 Vector3& dir) const {
			if (HomeAt(p))
				return false; // the button is over the cube
			for (const NaviFacet& f : NaviFacets()) {
				if (-Vector3::Dot(f.dir, view.forward) <= kFacingThreshold)
					continue; // back-facing
				Vector2 q[4];
				for (int i = 0; i < f.n; i++)
					q[i] = Project(view, f.v[i]);
				if (PointInPoly(p, q, f.n)) {
					dir = f.dir;
					return true;
				}
			}
			return false;
		}

		void NavigationCube::Draw(client::IRenderer& renderer, client::IFont& font,
		                          const GizmoView& view, const Vector2& cursor) const {
			const std::vector<NaviFacet>& facets = NaviFacets();
			Vector3 hdir;
			const bool hov = DirectionAt(view, cursor, hdir);
			// Draw bevels behind the faces: corners, then edges, then faces.
			for (int pass = 0; pass < 3; pass++) {
				for (const NaviFacet& f : facets) {
					bool isFace = f.tint >= 0;
					bool isCorner = (f.n == 3);
					bool isEdge = !isFace && !isCorner;
					if ((pass == 0 && !isCorner) || (pass == 1 && !isEdge) || (pass == 2 && !isFace))
						continue;
					float facing = -Vector3::Dot(f.dir, view.forward);
					if (facing <= kFacingThreshold)
						continue;
					Vector2 q[4];
					for (int i = 0; i < f.n; i++)
						q[i] = Project(view, f.v[i]);
					float sh = 0.45F + 0.55F * facing;
					Vector4 base = isFace ? AxisTint(f.tint) : MakeVector4(0.5F, 0.52F, 0.56F, 1.0F);
					bool hl = hov && Vector3::Dot(hdir, f.dir) > 0.999F;
					Vector4 col = hl ? kHighlight
					                 : MakeVector4(base.x * sh, base.y * sh, base.z * sh, 0.97F);
					// Each facet is one convex polygon so its whole outline is
					// anti-aliased; the seams it leaves against its neighbours are
					// covered by the edge lines drawn next.
					OverlayFillConvexPolygon(renderer, q, std::size_t(f.n), col);
					Vector4 ec = MakeVector4(0.08F, 0.08F, 0.1F, 0.85F);
					for (int e = 0; e < f.n; e++)
						OverlayStrokeLine(renderer, q[e], q[(e + 1) % f.n], 1.0F, ec);
				}
			}
			// Face labels last, so they sit on top of the cube.
			for (const NaviFacet& f : facets) {
				if (!f.label || -Vector3::Dot(f.dir, view.forward) <= kFacingThreshold)
					continue;
				Vector2 ctr = MakeVector2(0.0F, 0.0F);
				for (int i = 0; i < 4; i++)
					ctr += Project(view, f.v[i]);
				ctr = ctr * 0.25F;
				float ls = 0.85F;
				Vector2 ts = font.Measure(f.label);
				font.DrawShadow(f.label, ctr - MakeVector2(ts.x * ls * 0.5F, ts.y * ls * 0.5F), ls,
				                MakeVector4(1, 1, 1, 1), MakeVector4(0, 0, 0, 0.8F));
			}
			DrawHome(renderer, HomeAt(cursor));
		}
	} // namespace gui
} // namespace spades
