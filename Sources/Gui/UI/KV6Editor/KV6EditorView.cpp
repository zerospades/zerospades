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

#include "EditorUI.h"
#include "KV6EditorView.h"
#include "KV6DrawTool.h"
#include "KV6EditorTool.h"
#include "KV6ScreenHelper.h"
#include "KV6SubTool.h"
#include "KV6ToolRegistry.h"
#include <Gui/UI/Components/ColorPicker.h>
#include <Gui/UI/Components/Gizmo/GizmoCanvas.h>
#include <Gui/UI/Components/Gizmo/TransformGizmo.h>
#include <Gui/UI/Components/OptionBar.h>
#include <Gui/UI/Components/Toolbar.h>
#include <Gui/UIWidgetPainter.h>
#include <Gui/OverlayPaint.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <limits>
#include <tuple>

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IImage.h>
#include <Client/IModel.h>
#include <Client/ScreenShot.h>
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Core/LocalFileSystem.h>
#include <Core/VoxelModel.h>
#include <Gui/UI/Components/FileBrowser/FileBrowserDialog.h>
#include <Gui/UI/Components/UIOverlayHost.h>
#include <Gui/UI/Widgets/MessageBox.h>

SPADES_SETTING(cg_keyMoveForward);
SPADES_SETTING(cg_keyMoveBackward);
SPADES_SETTING(cg_keyMoveLeft);
SPADES_SETTING(cg_keyMoveRight);
SPADES_SETTING(cg_keyJump);
SPADES_SETTING(cg_keyCrouch);
SPADES_SETTING(cg_keySprint);
SPADES_SETTING(cg_keyScreenshot);

DEFINE_SPADES_SETTING(cg_keyDelete, "Delete");

namespace spades {
	namespace gui {
		namespace {
			// Mirror the in-game key matching (handles the "space" aliases) so the
			// editor honours the player's movement bindings.
			bool KV6CheckKey(const std::string& cfg, const std::string& input) {
				if (cfg.empty())
					return false;
				if (EqualsIgnoringCase(cfg, "space") || EqualsIgnoringCase(cfg, "spacebar") ||
				    EqualsIgnoringCase(cfg, "spacekey"))
					return input == " ";
				return EqualsIgnoringCase(cfg, input);
			}

			// Keys that form an editor shortcut together with Ctrl.
			bool IsCtrlShortcut(const std::string& key) {
				// Ctrl is also the descend key, so a chord here costs the movement
				// key bound to the same letter: keep this set clear of them.
				for (const char* k : {"s", "c", "x", "v", "z", "y"}) {
					if (EqualsIgnoringCase(key, k))
						return true;
				}
				return false;
			}

			float Clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

			// `v` in whole half-voxel steps, to the nearest step with ties going up.
			// A mirror plane `k` half steps from the origin maps voxel i to k - i,
			// and the plane snaps to the same count, so snapping never changes
			// which voxels a plane pairs up.
			int HalfSteps(float v) { return int(std::floor(v * 2.0F + 0.5F)); }

			// Camera speed factor while the sprint key (cg_keySprint) is held.
			constexpr float kSprintMultiplier = 3.0F;

			// How long a descend key that doubles as a chord modifier (Ctrl, the
			// default cg_keyCrouch) must be held alone before it moves the camera.
			// Long enough that even an unhurried Ctrl+Z never nudges the view, while
			// a deliberate hold still starts descending without feeling stuck.
			constexpr float kDescendGracePeriod = 0.4F;

			// The largest volume a model may grow to. A KV6 column is a 64-bit mask,
			// so no model can be deeper than 64 voxels.
			constexpr int kMaxModelWidth = 4096;
			constexpr int kMaxModelHeight = 4096;
			constexpr int kMaxModelDepth = 64;
			const char* const kMaxModelSizeMessage = "Reached the maximum model size";
			// Every edit that removes voxels refuses to take the last one.
			const char* const kLastVoxelMessage = "A model keeps at least one voxel";

			bool FitsModelSize(int w, int h, int d) {
				return w <= kMaxModelWidth && h <= kMaxModelHeight && d <= kMaxModelDepth;
			}

			// The editor's ground grid. It never moves: no model is deeper than
			// kMaxModelDepth, so a plane at that depth always sits below whatever is
			// being edited (voxel z spans [z-0.5, z+0.5], so the deepest possible
			// voxel ends half a voxel above it).
			constexpr float kGroundPlaneZ = float(kMaxModelDepth);
			constexpr int kGroundReach = 256; // voxels from the origin, each way
			constexpr int kGroundStep = 4;    // voxels between lines
			constexpr int kGroundMajorEvery = 4; // every Nth line is brighter

			// The far plane (and the fog that fades into it) has to sit beyond the
			// camera, or zooming out puts the whole model behind it.
			constexpr float kViewDistanceFactor = 4.0F;
			constexpr float kMinViewDistance = 1000.0F;

			// Edge length of the cube a new model's volume starts as.
			constexpr int kNewModelSize = 32;

			// Parts of a hint or help line; a line only ever breaks between them.
			const std::string kHintSeparator = "  |  ";

			// Splits `text` at its separators into lines no wider than `maxWidth`.
			// A part wider than that on its own gets a line to itself.
			std::vector<std::string> WrapParts(client::IFont& font, const std::string& text,
			                                   float maxWidth) {
				std::vector<std::string> lines;
				std::string line;
				std::size_t start = 0;
				while (true) {
					const std::size_t end = text.find(kHintSeparator, start);
					const std::string part = text.substr(start, end - start);
					const std::string joined = line.empty() ? part : line + kHintSeparator + part;
					if (!line.empty() && font.Measure(joined).x > maxWidth) {
						lines.push_back(line);
						line = part;
					} else {
						line = joined;
					}
					if (end == std::string::npos)
						break;
					start = end + kHintSeparator.size();
				}
				if (!line.empty())
					lines.push_back(line);
				return lines;
			}


			// Top UI bands (full width): a title ribbon above the toolbar. The 3D
			// viewport is inset below them by kBarsH.
			const float kRibbonH = 24.0F;
			const float kToolbarH = 32.0F;
			const float kSubBarH = 28.0F; // secondary toolbar (active tool's sub-tools)
			const float kBarsH = kRibbonH + kToolbarH + kSubBarH; // always-present bars

			// The 12 edges of an axis-aligned box spanning corners `a`..`b`, handed
			// one at a time to `emit(p, q)`. Shared by the cell/box outline overlays
			// and the viewport bounds, which differ only in how a line is drawn.
			template <class Emit>
			void BoxEdges(const Vector3& a, const Vector3& b, Emit emit) {
				for (int k = 0; k < 2; k++) {
					float z = (k == 0) ? a.z : b.z;
					emit(MakeVector3(a.x, a.y, z), MakeVector3(b.x, a.y, z));
					emit(MakeVector3(b.x, a.y, z), MakeVector3(b.x, b.y, z));
					emit(MakeVector3(b.x, b.y, z), MakeVector3(a.x, b.y, z));
					emit(MakeVector3(a.x, b.y, z), MakeVector3(a.x, a.y, z));
				}
				emit(MakeVector3(a.x, a.y, a.z), MakeVector3(a.x, a.y, b.z));
				emit(MakeVector3(b.x, a.y, a.z), MakeVector3(b.x, a.y, b.z));
				emit(MakeVector3(b.x, b.y, a.z), MakeVector3(b.x, b.y, b.z));
				emit(MakeVector3(a.x, b.y, a.z), MakeVector3(a.x, b.y, b.z));
			}

			// The viewport camera's near plane, which also cuts overlay lines.
			constexpr float kNearPlane = 0.1F;

			// Overlay lines (outlines, tool wires) are drawn as two-tone strokes, the
			// way editors keep selection outlines legible over any image: a dark
			// casing under a coloured core, so a line reads against light voxels by
			// its casing and against dark ones by its core. Widths in window units.
			constexpr float kOverlayCoreWidth = 2.0F;
			constexpr float kOverlayCasingWidth = 4.0F;
			constexpr float kOverlayCasingAlpha = 0.8F;
			// Where voxels hide a line it is drawn thin and dim, with no casing, so
			// it shows through without being taken for a visible edge.
			constexpr float kOverlayHiddenWidth = 1.0F;
			constexpr float kOverlayHiddenAlpha = 0.35F;
			// A line shorter than this on screen is drawn thinner, down to
			// kOverlayMinWidthScale, so a distant voxel grid does not fill in with
			// casing.
			constexpr float kOverlayFullWidthLength = 16.0F;
			constexpr float kOverlayMinWidthScale = 0.5F;
			// Whether voxels hide a line is tested every this many window units
			// along it, at least once and at most kOverlayMaxSamples times.
			constexpr float kOverlaySampleSpacing = 6.0F;
			constexpr int kOverlayMaxSamples = 256;
			// How far short of a line point its occlusion ray stops, in world units,
			// so the voxel whose edge the line follows never hides it.
			constexpr float kOcclusionSlack = 0.01F;

			// Cut segment a-b down to the part on the side of a plane through the eye
			// where `In` is positive. False when nothing is left.
			template <class In> bool ClipToPlane(In in, Vector3& a, Vector3& b) {
				const float ia = in(a);
				const float ib = in(b);
				if (ia < 0.0F && ib < 0.0F)
					return false;
				if (ia < 0.0F)
					a = a + (b - a) * (ia / (ia - ib));
				else if (ib < 0.0F)
					b = b + (a - b) * (ib / (ib - ia));
				return true;
			}

			// Cut segment a-b down to the part within `margin` window units of the
			// viewport, the near plane included. False when nothing is left, so a
			// line's screen length, and the work spent along it, covers what shows.
			bool ClipToView(const GizmoView& view, float margin, Vector3& a, Vector3& b) {
				const Vector3 eye = view.eye;
				if (!ClipToPlane([&](const Vector3& p) { return view.Depth(p) - kNearPlane; }, a, b))
					return false;
				// The sides, widened by the margin, as planes through the eye: a point
				// is inside when its offset across the view is within the frustum's
				// half-width at its depth.
				const float halfWidth =
				  view.tanHalfFovX * (1.0F + 2.0F * margin / view.viewportWidth);
				const float halfHeight =
				  view.tanHalfFovY * (1.0F + 2.0F * margin / view.viewportHeight);
				const Vector3 sides[4] = {view.right, view.right * -1.0F, view.up, view.up * -1.0F};
				for (int k = 0; k < 4; k++) {
					const Vector3& across = sides[k];
					const float half = (k < 2) ? halfWidth : halfHeight;
					if (!ClipToPlane(
					      [&](const Vector3& p) {
						      const Vector3 rel = p - eye;
						      return Vector3::Dot(rel, view.forward) * half - Vector3::Dot(rel, across);
					      },
					      a, b))
						return false;
				}
				return true;
			}

			// Whether two views would put everything in the same place on screen.
			bool SameView(const GizmoView& a, const GizmoView& b) {
				return a.eye == b.eye && a.right == b.right && a.up == b.up && a.forward == b.forward &&
				       a.tanHalfFovX == b.tanHalfFovX && a.tanHalfFovY == b.tanHalfFovY &&
				       a.viewportX == b.viewportX && a.viewportY == b.viewportY &&
				       a.viewportWidth == b.viewportWidth && a.viewportHeight == b.viewportHeight;
			}

			// Segment a-b lengthened by `reach` at both ends, so strokes meeting at a
			// corner overlap there instead of leaving a notch.
			void Lengthen(Vector2& a, Vector2& b, float reach) {
				const Vector2 d = b - a;
				const float length = d.GetLength();
				if (length <= 0.0F)
					return;
				const Vector2 step = d * (reach / length);
				a -= step;
				b += step;
			}

			// The voxels that hide overlay lines: the document's, and the pending ones
			// rendered solid where they would land.
			class OverlayOccluders {
			public:
				// `pending`, when there is any, is the pending voxels' own grid, whose
				// (0,0,0) sits at `pendingAnchor` in the document.
				OverlayOccluders(const VoxelModel& model, const VoxelModel* pending,
				                 const IntVector3& pendingAnchor)
				    : model(model),
				      pending(pending),
				      pendingAnchor(pendingAnchor),
				      lo(MakeIntVector3(0, 0, 0)),
				      hi(MakeIntVector3(model.GetWidth() - 1, model.GetHeight() - 1,
				                        model.GetDepth() - 1)) {
					if (!pending)
						return;
					const IntVector3 last =
					  pendingAnchor + MakeIntVector3(pending->GetWidth() - 1, pending->GetHeight() - 1,
					                                 pending->GetDepth() - 1);
					lo = MakeIntVector3(std::min(lo.x, pendingAnchor.x), std::min(lo.y, pendingAnchor.y),
					                    std::min(lo.z, pendingAnchor.z));
					hi = MakeIntVector3(std::max(hi.x, last.x), std::max(hi.y, last.y),
					                    std::max(hi.z, last.z));
				}

				// Whether a voxel lies between `eye` and `point`.
				bool Hide(const Vector3& eye, const Vector3& point) const {
					const Vector3 toPoint = point - eye;
					const float distance = toPoint.GetLength();
					float tEnd = distance - kOcclusionSlack;
					if (tEnd <= 0.0F)
						return false;
					const Vector3 dir = toPoint * (1.0F / distance);
					const float o[3] = {eye.x, eye.y, eye.z};
					const float d[3] = {dir.x, dir.y, dir.z};
					const int cellLo[3] = {lo.x, lo.y, lo.z};
					const int cellHi[3] = {hi.x, hi.y, hi.z};

					// Keep to the part of the ray inside the occluders' bounds. Voxel i
					// is centred at i, so it spans i - 0.5 .. i + 0.5.
					float tStart = 0.0F;
					for (int k = 0; k < 3; k++) {
						const float boundLo = float(cellLo[k]) - 0.5F;
						const float boundHi = float(cellHi[k]) + 0.5F;
						if (d[k] == 0.0F) {
							if (o[k] < boundLo || o[k] > boundHi)
								return false;
							continue;
						}
						float t0 = (boundLo - o[k]) / d[k];
						float t1 = (boundHi - o[k]) / d[k];
						if (t0 > t1)
							std::swap(t0, t1);
						tStart = std::max(tStart, t0);
						tEnd = std::min(tEnd, t1);
					}
					if (tStart >= tEnd)
						return false;

					// Walk the voxels the ray crosses, as DoPick does, in a space shifted
					// by half a voxel where voxel i spans i .. i + 1.
					int cell[3], step[3];
					float tNext[3], tDelta[3];
					for (int k = 0; k < 3; k++) {
						const float at = o[k] + d[k] * tStart + 0.5F;
						cell[k] = std::max(cellLo[k], std::min(cellHi[k], int(std::floor(at))));
						step[k] = d[k] >= 0.0F ? 1 : -1;
						if (d[k] == 0.0F) {
							tNext[k] = tDelta[k] = std::numeric_limits<float>::infinity();
							continue;
						}
						const float boundary = float(cell[k]) + (step[k] > 0 ? 1.0F : 0.0F);
						tNext[k] = tStart + (boundary - at) / d[k];
						tDelta[k] = std::fabs(1.0F / d[k]);
					}
					float tCell = tStart; // where the ray enters `cell`
					// The voxel the eye is inside hides nothing: the renderer shows its
					// faces from the outside only, so the scene behind it is in view.
					bool eyeCell = tStart == 0.0F;
					while (tCell < tEnd) {
						if (!eyeCell && Solid(cell[0], cell[1], cell[2]))
							return true;
						eyeCell = false;
						const int k = (tNext[0] <= tNext[1] && tNext[0] <= tNext[2]) ? 0
						              : (tNext[1] <= tNext[2])                      ? 1
						                                                             : 2;
						tCell = tNext[k];
						tNext[k] += tDelta[k];
						cell[k] += step[k];
						if (cell[k] < cellLo[k] || cell[k] > cellHi[k])
							return false;
					}
					return false;
				}

			private:
				const VoxelModel& model;
				const VoxelModel* pending;
				IntVector3 pendingAnchor;
				IntVector3 lo, hi; // bounds of every occluding voxel, inclusive

				bool Solid(int x, int y, int z) const {
					if (model.IsSolid(x, y, z))
						return true;
					if (!pending)
						return false;
					const IntVector3 rel = MakeIntVector3(x, y, z) - pendingAnchor;
					return pending->IsSolid(rel.x, rel.y, rel.z); // bounds-checked
				}
			};

			// The six voxels sharing a face with a voxel, as offsets.
			const IntVector3 kFaceNeighbours[6] = {
			  MakeIntVector3(1, 0, 0),  MakeIntVector3(-1, 0, 0), MakeIntVector3(0, 1, 0),
			  MakeIntVector3(0, -1, 0), MakeIntVector3(0, 0, 1),  MakeIntVector3(0, 0, -1)};

			// The smallest box holding `cells`; false when there are none.
			bool BoundsOf(const std::vector<IntVector3>& cells, IntVector3& lo, IntVector3& hi) {
				if (cells.empty())
					return false;
				lo = hi = cells.front();
				for (const IntVector3& c : cells) {
					lo = MakeIntVector3(std::min(lo.x, c.x), std::min(lo.y, c.y), std::min(lo.z, c.z));
					hi = MakeIntVector3(std::max(hi.x, c.x), std::max(hi.y, c.y), std::max(hi.z, c.z));
				}
				return true;
			}

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
		} // namespace

		KV6EditorView::KV6EditorView(client::IRenderer* r, client::IAudioDevice* dev,
		                             client::FontManager* fm, SoftwareCursor* c,
		                             const std::string& path, bool isNew)
		    : renderer(r), audioDevice(dev), fontManager(fm) {
			SPADES_MARK_FUNCTION();
			if (c) {
				softwareCursor = c;
			} else {
				ownedCursor = std::make_unique<SoftwareCursor>(*renderer);
				softwareCursor = ownedCursor.get();
			}
			io = Handle<KV6ScreenHelper>::New();

			// Initialize UI manager
			ui = Handle<EditorUI>::New(renderer.GetPointerOrNull(), audioDevice.GetPointerOrNull(),
			                           &*fontManager, this, softwareCursor);

			// Wire up toolbar callbacks. Buttons report ids, resolved against the
			// modes and tools as they are now; the toolbar only reports clicks on
			// enabled buttons.
			ui->GetToolbar()->OnModeClicked = [this](const std::string& id) {
				for (const ModeInfo& info : Modes()) {
					if (id == info.id && ModeSupported(info.mode))
						SetMode(info.mode);
				}
			};
			ui->GetToolbar()->OnToolClicked = [this](const std::string& id) {
				SetActiveTool(ToolIndex(id));
			};
			ui->GetToolbar()->OnUndoClicked = [this]() { Undo(); };
			ui->GetToolbar()->OnRedoClicked = [this]() { Redo(); };

			// Wire up option bar callbacks. The bar's content belongs to the
			// active tool, so a click only counts while that tool still is.
			ui->GetOptionBar()->CurrentOwner = [this] {
				return static_cast<const void*>(ActiveTool());
			};
			ui->GetOptionBar()->OnSubToolClicked = [this](int index) {
				if (EditorTool* tool = ActiveTool())
					tool->SetSubTool(*this, index);
			};
			ui->GetOptionBar()->OnBoolToggled = [this](const std::string& id) {
				if (ToolOption* opt = ActiveOption(id, ToolOption::Type::Bool)) {
					opt->bvalue = !opt->bvalue;
					ActiveTool()->OnOptionToggled(*this, id, opt->bvalue);
				}
			};
			ui->GetOptionBar()->OnActionClicked = [this](const std::string& id) {
				if (ActiveOption(id, ToolOption::Type::Action))
					ActiveTool()->OnAction(*this, id);
			};

			// The brush colour is the editor's, shared by every tool: its swatch
			// sits on the main toolbar and opens or closes the picker, which
			// edits it and stays up across tool switches until closed.
			ui->GetToolbar()->OnColorSwatchClicked = [this] {
				ColorPicker& picker = *ui->GetColorPicker();
				if (picker.IsOpen()) {
					picker.Close();
				} else {
					picker.SetColor(currentColor);
					picker.Open();
				}
			};
			ui->GetColorPicker()->OnColorChanged = [this](uint32_t c) { currentColor = c; };

			// The Edit-mode tools come from the registry (toolbar order = registration).
			// Open on Draw, so the user can start modelling; found by type, so
			// no button label decides it.
			ToolRegistry::Instance().BuildAll(tools);
			activeTool = 0;
			for (size_t i = 0; i < tools.size(); i++) {
				if (dynamic_cast<DrawTool*>(tools[i].tool.get())) {
					activeTool = int(i);
					break;
				}
			}

			if (isNew || path.empty())
				NewModel(kNewModelSize, path);
			else
				LoadModel(path);
		}

		KV6EditorView::~KV6EditorView() { SPADES_MARK_FUNCTION(); }

		// --- Document ---------------------------------------------------------

		void KV6EditorView::FrameCamera() {
			orbitTarget = MakeVector3(model->GetWidth() * 0.5F - 0.5F, model->GetHeight() * 0.5F - 0.5F,
			                          model->GetDepth() * 0.5F - 0.5F);
			orbitDist =
			  float(std::max(model->GetWidth(), std::max(model->GetHeight(), model->GetDepth()))) *
			  1.8F;
		}

		void KV6EditorView::ResetDocumentState() {
			// Nothing of the old document's edit state carries over but which
			// axes mirror: that is how the user works, not part of the document.
			MirrorSetup kept;
			for (int a = 0; a < 3; a++)
				kept.enabled[a] = edit.mirror.enabled[a];
			edit = EditState();
			edit.mirror = kept;
			previewedOrigin.reset();
			previewedMirrorPlane.reset();
			undo.Clear();
			InvalidateRenderModel(); // a new model is on its way
		}

		void KV6EditorView::NewModel(int n, const std::string& path) {
			ResetDocumentState();
			cubeSize = n;
			model = Handle<VoxelModel>::New(n, n, n);
			model->SetSolid(n / 2, n / 2, n / 2, currentColor);
			// Anchor the pivot on the seed voxel so mirroring (whose planes start
			// there) is symmetric about it from the start.
			float c = float(n / 2);
			model->SetOrigin(MakeVector3(-c, -c, -c));
			voxelCount = 1;
			PlaceMirrorPlane(GetPivot());
			filePath = path;
			FrameCamera();
			savedGeomId = -1; // a fresh, never-saved document starts dirty
			NotifyDocumentChanged();
		}

		void KV6EditorView::LoadModel(const std::string& path) {
			VoxelModel* loaded = io->Load(path);
			if (!loaded) {
				NewModel(kNewModelSize, path);
				SetStatus("Could not load " + path);
				return;
			}
			ResetDocumentState();
			model = Handle<VoxelModel>(loaded, false); // adopt (Load returns a ref)
			cubeSize = std::max(model->GetWidth(), std::max(model->GetHeight(), model->GetDepth()));
			voxelCount = CountSolids();
			PlaceMirrorPlane(GetPivot());
			filePath = path;
			FrameCamera();
			savedGeomId = undo.GeometryStateId(); // a freshly loaded document is clean
			NotifyDocumentChanged();
		}

		int KV6EditorView::CountSolids() {
			int count = 0;
			for (int x = 0; x < model->GetWidth(); x++)
			for (int y = 0; y < model->GetHeight(); y++)
			for (int z = 0; z < model->GetDepth(); z++) {
				if (model->IsSolid(x, y, z))
					count++;
			}
			return count;
		}

		bool KV6EditorView::InBounds(int x, int y, int z) const {
			return x >= 0 && y >= 0 && z >= 0 && x < model->GetWidth() && y < model->GetHeight() &&
			       z < model->GetDepth();
		}

		void KV6EditorView::RefreshRenderModels() {
			if (renderModelDirty) {
				renderModel = renderer->CreateModel(*model);
				renderModelDirty = false;
			}
			// The pending voxels' model follows their voxels' version, so any
			// change to them (a turn, an undo, a new paste) rebuilds it and
			// nothing else does.
			const std::uint64_t pendingVersion = edit.placement.voxels.Version();
			if (!edit.placing) {
				placementModel = Handle<client::IModel>();
				placementVoxels = Handle<VoxelModel>();
			} else if (!placementModel || placementModelVersion != pendingVersion) {
				const IntVector3 extent = edit.placement.Extent();
				Handle<VoxelModel> preview = Handle<VoxelModel>::New(extent.x, extent.y, extent.z);
				for (const ClipVoxel& v : *edit.placement.voxels)
					preview->SetSolid(v.rel.x, v.rel.y, v.rel.z, v.color);
				placementModel = renderer->CreateModel(*preview);
				placementVoxels = preview;
				placementModelVersion = pendingVersion;
			}
		}

		void KV6EditorView::SetStatus(const std::string& s) {
			statusMessage = s;
			statusTimer = 2.5F;
		}

		bool KV6EditorView::Save() {
			// Saving writes the document, so anything still pending belongs in it.
			DocumentCommand command(*this);
			if (filePath.empty()) {
				SetStatus("No file to save to");
				return false;
			}
			if (!io->Save(&*model, filePath)) {
				SetStatus("Save failed");
				return false;
			}
			savedGeomId = undo.GeometryStateId(); // this geometry state is now clean
			SetStatus("Saved " + filePath);
			return true;
		}

		std::vector<EditorMenuItem> KV6EditorView::GetMenuItems() {
			auto newModel = [this] {
				ConfirmDiscardChanges([this] {
					NewModel(kNewModelSize, std::string());
					SetStatus("New model");
				});
			};
			std::vector<EditorMenuItem> items;
			items.push_back(EditorMenuItem{"New Model", newModel, true});
			items.push_back(EditorMenuItem{"Open Model...", [this] { OpenDocument(); }, true});
			items.push_back(EditorMenuItem{"Import Model...", [this] { OpenImportDialog(); }, true});
			items.push_back(EditorMenuItem{"Save", [this] { Save(); }, !filePath.empty()});
			items.push_back(EditorMenuItem{"Save As...", [this] { OpenSaveAsDialog(); }, true});
			items.push_back(EditorMenuItem{
			  "Exit to Menu", [this] { ConfirmDiscardChanges([this] { wantsClose = true; }); }, true});
			return items;
		}

		void KV6EditorView::ConfirmDiscardChanges(std::function<void()> proceed) {
			if (!proceed)
				return;
			if (!HasUnsavedChanges()) {
				proceed();
				return;
			}

			UIOverlayHost* overlay = ui->GetOverlay();
			Handle<MessageBoxScreen> box = Handle<MessageBoxScreen>::New(
			  &overlay->GetUIManager().GetRootElement(),
			  "This model has unsaved changes.",
			  std::vector<std::string>{"Save", "Discard", "Cancel"}, 160.0F);
			box->closed = [this, proceed](ui::UIElement& sender) {
				MessageBoxScreen* answer = dynamic_cast<MessageBoxScreen*>(&sender);
				if (!answer)
					return;
				if (answer->resultIndex == 1) { // Discard
					proceed();
				} else if (answer->resultIndex == 0) { // Save
					// A document that was never saved needs somewhere to go first;
					// `proceed` then runs only if that save succeeds.
					if (filePath.empty())
						OpenSaveAsDialog(proceed);
					else if (Save())
						proceed();
				}
			};
			ReleaseHeldInput();
			overlay->Show(box.GetPointerOrNull());
		}

		void KV6EditorView::ShowModelFileDialog(const std::string& title, FileBrowserPurpose purpose,
		                                        const std::string& initialName,
		                                        std::function<void(const std::string&)> picked) {
			FileBrowserOptions options;
			options.purpose = purpose;
			options.target = FileBrowserTarget::Files;
			options.homeDir = io->DefaultDir();
			options.initialDir =
			  filePath.empty() ? io->DefaultDir() : LocalFileSystem::ParentDir(filePath);
			options.initialName = initialName;
			options.filters.push_back(FileFilter{"KV6 models", {GetDocumentExtension()}});

			UIOverlayHost* overlay = ui->GetOverlay();
			Handle<FileBrowserDialog> dialog = Handle<FileBrowserDialog>::New(
			  &overlay->GetUIManager().GetRootElement(), title, std::move(options));
			dialog->closed = [picked](const FileBrowserResult& result) {
				if (result.accepted && !result.paths.empty())
					picked(result.paths.front());
			};
			// A dialog takes over input: nothing should stay held while it is up.
			ReleaseHeldInput();
			overlay->Show(dialog.GetPointerOrNull());
		}

		void KV6EditorView::OpenDocument() {
			ConfirmDiscardChanges([this] {
				ShowModelFileDialog("Open Model", FileBrowserPurpose::Open, std::string(),
				                    [this](const std::string& path) { LoadModel(path); });
			});
		}

		void KV6EditorView::OpenSaveAsDialog(std::function<void()> after) {
			const std::string name =
			  filePath.empty() ? "untitled.kv6" : LocalFileSystem::GetFileName(filePath);
			ShowModelFileDialog("Save As", FileBrowserPurpose::Save, name,
			                    [this, after](const std::string& path) {
				                    if (SaveDocument(path) && after)
					                    after();
			                    });
		}

		void KV6EditorView::OpenImportDialog() {
			ShowModelFileDialog("Import Model", FileBrowserPurpose::Open, std::string(),
			                    [this](const std::string& path) { ImportModel(path); });
		}

		bool KV6EditorView::SaveDocument(const std::string& path) {
			filePath = path;
			return Save();
		}

		// --- Escape -----------------------------------------------------------

		KV6EditorView::EscapeLayer KV6EditorView::NextEscape() {
			const ColorPicker& picker = *ui->GetColorPicker();
			if (picker.GetEyedropperMode())
				return EscapeLayer::Eyedropper;
			if (EditorTool* tool = ActiveTool()) {
				if (!tool->EscapeLabel(*this).empty())
					return EscapeLayer::Tool;
			}
			if (edit.placing)
				return EscapeLayer::Placement;
			if (!edit.selection.Empty())
				return EscapeLayer::Selection;
			return EscapeLayer::None;
		}

		std::string KV6EditorView::EscapeLabel(EscapeLayer layer) {
			switch (layer) {
				case EscapeLayer::Eyedropper: return "stop picking";
				case EscapeLayer::Tool: return ActiveTool()->EscapeLabel(*this);
				case EscapeLayer::Placement:
					return edit.placement.lifted->empty() ? "drop the pending voxels"
					                                      : "put the voxels back";
				case EscapeLayer::Selection: return "select nothing";
				case EscapeLayer::None: break;
			}
			return "menu";
		}

		void KV6EditorView::Escape(EscapeLayer layer) {
			switch (layer) {
				case EscapeLayer::Eyedropper: ui->GetColorPicker()->SetEyedropperMode(false); break;
				case EscapeLayer::Tool: ActiveTool()->OnEscape(*this); break;
				case EscapeLayer::Placement: CancelPlacement(); break;
				case EscapeLayer::Selection:
					ClearSelection();
					SetStatus("Selection cleared");
					break;
				case EscapeLayer::None: break;
			}
		}

		bool KV6EditorView::OnMenuEscape() {
			// The menu asks before opening, so it only opens once nothing is left
			// to back out of.
			const EscapeLayer layer = NextEscape();
			Escape(layer);
			return layer != EscapeLayer::None;
		}

		// --- Camera -----------------------------------------------------------

		Vector3 KV6EditorView::Forward() const {
			float cp = cosf(pitch);
			return MakeVector3(cp * cosf(yaw), cp * sinf(yaw), -sinf(pitch));
		}

		Vector3 KV6EditorView::CameraEye() const { return orbitTarget - Forward() * orbitDist; }

		float KV6EditorView::ViewDistance() const {
			// Far enough to keep the model visible at any zoom, and never nearer than
			// the fixed distance the editor used before.
			return std::max(kMinViewDistance, orbitDist * kViewDistanceFactor);
		}

		void KV6EditorView::UpdateMovement(float dt) {
			Vector3 fwd = Forward();
			Vector3 up = MakeVector3(0.0F, 0.0F, -1.0F);
			// Cross(fwd, up) normalised, derived from the heading so it stays valid
			// when looking straight up/down (navicube top/bottom), where the cross
			// product collapses. Matches the camera side axis in SetupScene.
			Vector3 right = MakeVector3(-sinf(yaw), cosf(yaw), 0.0F);

			float step = float(cubeSize) * 0.7F * (keySprint ? kSprintMultiplier : 1.0F) * dt;
			bool descending = keyDown && DescendKeyIsActive();
			auto displacement = [&](bool withDescend) {
				Vector3 move = MakeVector3(0, 0, 0);
				if (keyFwd) move += fwd;
				if (keyBack) move -= fwd;
				if (keyRight) move += right;
				if (keyLeft) move -= right;
				if (keyUp) move += up;
				if (withDescend && descending) move -= up;
				if (move.x == 0.0F && move.y == 0.0F && move.z == 0.0F)
					return MakeVector3(0, 0, 0);
				return move.Normalize() * step;
			};

			Vector3 delta = displacement(true);
			// Moving the orbited point carries the whole view along with it.
			orbitTarget += delta;

			// While Ctrl is both the descend key and held, remember how far the
			// descend part alone moved us, so a Ctrl shortcut can take it back.
			if (descending && ctrlHeld && KV6CheckKey(cg_keyCrouch, "Control"))
				ctrlDescent += delta - displacement(false);
		}

		bool KV6EditorView::DescendKeyIsActive() const {
			// A descend key that cannot start a shortcut moves the camera at once.
			if (!KV6CheckKey(cg_keyCrouch, "Control"))
				return true;
			// Ctrl also opens Ctrl+S/C/X/V/Z/Y, so wait out the moment in which the
			// letter of a chord would arrive: the view then never moves for a
			// shortcut, and a deliberate hold still descends.
			return globalTime - descendPressTime >= kDescendGracePeriod;
		}

		void KV6EditorView::PanView(float dx, float dy) {
			if (!camera.IsValid())
				return;
			// World units per screen pixel at the orbited point, so the point under
			// the cursor follows the drag at any zoom level.
			float unitsPerPixel = camera.WorldPerPixel(orbitTarget);
			orbitTarget +=
			  camera.right * (-dx * unitsPerPixel) + camera.up * (dy * unitsPerPixel);
		}

		void KV6EditorView::ReleaseHeldInput() {
			keyFwd = keyBack = keyLeft = keyRight = keyUp = keyDown = false;
			ctrlDescent = MakeVector3(0, 0, 0);
			lookActive = false;
			// The release of a held button may be swallowed too, so the press it
			// began is over: the tool and the colour picker drop their drags and
			// nothing reads as dragged.
			lmbHeld = rmbHeld = false;
			if (ui)
				ui->GetColorPicker()->MouseUp();
			CancelToolInteraction();
		}

		client::SceneDefinition KV6EditorView::SetupScene(float vpX, float vpY, float vpW, float vpH) {
			client::SceneDefinition sceneDef;
			Vector3 eye = CameraEye();
			Vector3 at = orbitTarget;
			Vector3 up = MakeVector3(0.0F, 0.0F, -1.0F);

			Vector3 dir = (at - eye).Normalize();
			// At the navicube Top/Bottom views dir is parallel to the world-up
			// reference, so Cross(dir, up) collapses to NaN. For every non-vertical
			// pitch Cross(dir, up).Normalize() equals (-sin(yaw), cos(yaw), 0); use
			// that heading-derived value at the pole too. It is the continuous limit
			// of the cross product, so the camera reaches top/bottom smoothly instead
			// of snapping, and only flips if rotated past the pole.
			Vector3 side;
			if (std::fabs(Vector3::Dot(dir, up)) > 0.999F)
				side = MakeVector3(-sinf(yaw), cosf(yaw), 0.0F);
			else
				side = Vector3::Cross(dir, up).Normalize();
			up = -Vector3::Cross(dir, side);

			sceneDef.viewOrigin = eye;
			sceneDef.viewAxis[0] = side;
			sceneDef.viewAxis[1] = up;
			sceneDef.viewAxis[2] = dir;
			sceneDef.fovY = 60.0F * M_PI_F / 180.0F;
			sceneDef.fovX = 2.0F * atanf(tanf(sceneDef.fovY * 0.5F) * (vpW / vpH));
			sceneDef.zNear = kNearPlane;
			sceneDef.zFar = ViewDistance();
			sceneDef.viewportLeft = int(vpX);
			sceneDef.viewportTop = int(vpY);
			sceneDef.viewportWidth = int(vpW);
			sceneDef.viewportHeight = int(vpH);
			// The editor draws its own ground: the game world brings chunk culling
			// and terrain that crowds the model, and water is a plane following the
			// camera at sea level with a mirrored scene rendered into it.
			sceneDef.skipWorld = true;
			sceneDef.skipWater = true;
			// A voxel has six face normals, so sunlight splits one palette colour
			// into several on-screen shades and the model stops reading as the
			// colour it was painted. Light it evenly instead, and outline it so the
			// shape survives the loss of shading.
			sceneDef.flatModelLighting = true;
			sceneDef.forceOutlines = true;
			sceneDef.denyCameraBlur = true;
			sceneDef.time = (unsigned int)(globalTime * 1000.0F);
			return sceneDef;
		}

		// --- Editing ----------------------------------------------------------

		bool KV6EditorView::RayPlaneCell(const Vector3& planePoint, const Vector3& normal,
		                                 IntVector3& out) {
			if (!camera.IsValid())
				return false;
			Vector3 origin, dir;
			camera.Ray(softwareCursor->GetPosition(), origin, dir);
			float denom = Vector3::Dot(dir, normal);
			if (std::fabs(denom) < 1.0e-5F)
				return false;
			float t = Vector3::Dot(planePoint - origin, normal) / denom;
			if (t <= 0.0F)
				return false;
			Vector3 hit = origin + dir * t;
			out = MakeIntVector3(int(std::floor(hit.x + 0.5F)), int(std::floor(hit.y + 0.5F)),
			                     int(std::floor(hit.z + 0.5F)));
			return true;
		}

		Vector2 KV6EditorView::WorldToScreen(const Vector3& w, bool& ok) const {
			Vector2 screen = MakeVector2(0.0F, 0.0F);
			ok = camera.Project(w, screen);
			return screen;
		}

		// Collected for DrawOverlayLines2D, which draws it once the scene is done.
		void KV6EditorView::EmitLine(const Vector3& a, const Vector3& b, const Vector4& color) {
			overlayLines.push_back({a, b, color});
		}

		void KV6EditorView::DrawLine3D(const Vector3& a, const Vector3& b, const Vector4& color) {
			EmitLine(a, b, color);
		}

		// Work out where this frame's lines run on screen and what hides them, as
		// the strokes to draw. Kept from frame to frame: the strokes only change
		// when the lines, the view or the voxels do, and this is the costly part.
		void KV6EditorView::UpdateOverlayLines() {
			// The same edge is often emitted more than once (neighbouring cell
			// outlines share theirs): it is tested and drawn once. A line's
			// direction does not matter, so each is put the same way round first.
			auto before = [](const Vector3& p, const Vector3& q) {
				return std::tie(p.x, p.y, p.z) < std::tie(q.x, q.y, q.z);
			};
			auto key = [](const OverlayLine& l) {
				return std::tie(l.a.x, l.a.y, l.a.z, l.b.x, l.b.y, l.b.z, l.color.x, l.color.y,
				                l.color.z, l.color.w);
			};
			auto same = [&](const OverlayLine& l, const OverlayLine& r) {
				return key(l) == key(r);
			};
			for (OverlayLine& l : overlayLines) {
				if (before(l.b, l.a))
					std::swap(l.a, l.b);
			}
			const bool linesChanged =
			  overlayLines.size() != overlayCache.emitted.size() ||
			  !std::equal(overlayLines.begin(), overlayLines.end(), overlayCache.emitted.begin(), same);
			if (linesChanged) {
				overlayCache.emitted = overlayLines;
				overlayCache.lines = overlayLines;
				std::stable_sort(overlayCache.lines.begin(), overlayCache.lines.end(),
				                 [&](const OverlayLine& l, const OverlayLine& r) {
					                 return key(l) < key(r);
				                 });
				overlayCache.lines.erase(
				  std::unique(overlayCache.lines.begin(), overlayCache.lines.end(), same),
				  overlayCache.lines.end());
			}

			const IntVector3 pendingAnchor =
			  edit.placing ? edit.placement.anchor : IntVector3::Make(0, 0, 0);
			if (overlayCache.valid && !linesChanged && SameView(overlayCache.view, camera) &&
			    overlayCache.voxelsVersion == voxelsVersion && overlayCache.placing == edit.placing &&
			    overlayCache.pendingVersion == placementModelVersion &&
			    overlayCache.pendingAnchor == pendingAnchor)
				return; // nothing the strokes depend on has moved
			overlayCache.view = camera;
			overlayCache.voxelsVersion = voxelsVersion;
			overlayCache.placing = edit.placing;
			overlayCache.pendingVersion = placementModelVersion;
			overlayCache.pendingAnchor = pendingAnchor;
			overlayCache.valid = true;
			overlayCache.hidden.clear();
			overlayCache.shown.clear();

			std::vector<OverlayStroke>& hidden = overlayCache.hidden;
			std::vector<OverlayStroke>& shown = overlayCache.shown;
			const VoxelModel* pending = edit.placing ? placementVoxels.GetPointerOrNull() : nullptr;
			const OverlayOccluders occluders(*model, pending, pendingAnchor);
			std::vector<Vector2> ends; // screen points splitting a line into pieces
			for (const OverlayLine& l : overlayCache.lines) {
				Vector3 a = l.a, b = l.b;
				if (!ClipToView(camera, kOverlayCasingWidth, a, b))
					continue;
				Vector2 pa, pb;
				if (!camera.Project(a, pa) || !camera.Project(b, pb))
					continue;

				const float screenLength = (pb - pa).GetLength();
				const float widthScale =
				  Clampf(screenLength / kOverlayFullWidthLength, kOverlayMinWidthScale, 1.0F);
				const int pieces = std::max(
				  1, std::min(kOverlayMaxSamples, int(std::ceil(screenLength / kOverlaySampleSpacing))));

				// Each piece is in view or hidden as its middle is; neighbouring
				// pieces alike make one stroke.
				ends.assign(1, pa);
				bool projected = true;
				for (int p = 1; p < pieces && projected; p++) {
					Vector2 s;
					projected = camera.Project(a + (b - a) * (float(p) / float(pieces)), s);
					ends.push_back(s);
				}
				if (!projected)
					continue; // cannot happen past the near plane, but never draw a guess
				ends.push_back(pb);

				Vector4 hiddenColor = l.color;
				hiddenColor.w *= kOverlayHiddenAlpha;
				int runStart = 0;
				bool runHidden = false;
				for (int p = 0; p <= pieces; p++) {
					bool pieceHidden = runHidden;
					if (p < pieces) {
						const Vector3 middle = a + (b - a) * ((float(p) + 0.5F) / float(pieces));
						pieceHidden = occluders.Hide(camera.eye, middle);
						if (p == 0)
							runHidden = pieceHidden;
					}
					if (p == pieces || pieceHidden != runHidden) {
						if (runHidden)
							hidden.push_back({ends[runStart], ends[p], 1.0F, hiddenColor});
						else
							shown.push_back({ends[runStart], ends[p], widthScale, l.color});
						runStart = p;
						runHidden = pieceHidden;
					}
				}
			}
		}

		// Draw the lines collected this frame over the finished scene: a casing and
		// a core where they are in view, a dim core where voxels hide them.
		// Visibility is worked out against the voxels rather than left to the depth
		// buffer, as outlines lie on voxel faces and would fight them for depth, and
		// a 3D line cannot be given a casing or a width.
		void KV6EditorView::DrawOverlayLines2D() {
			if (!camera.IsValid()) {
				overlayLines.clear();
				return;
			}
			UpdateOverlayLines();
			overlayLines.clear();

			// Hidden lines first, so every line in view is over them, and all the
			// casings before any core, so no casing covers a core it meets.
			for (const OverlayStroke& s : overlayCache.hidden)
				DrawLine2D(s.a, s.b, kOverlayHiddenWidth, s.color);
			for (const OverlayStroke& s : overlayCache.shown) {
				const float width = kOverlayCasingWidth * s.widthScale;
				Vector2 a = s.a, b = s.b;
				Lengthen(a, b, width * 0.5F);
				DrawLine2D(a, b, width, MakeVector4(0.0F, 0.0F, 0.0F, kOverlayCasingAlpha * s.color.w));
			}
			for (const OverlayStroke& s : overlayCache.shown) {
				const float width = kOverlayCoreWidth * s.widthScale;
				Vector2 a = s.a, b = s.b;
				Lengthen(a, b, width * 0.5F);
				DrawLine2D(a, b, width, s.color);
			}
		}

		void KV6EditorView::DoPick() {
			pickHit = false;
			if (!camera.IsValid())
				return;

			Vector3 eye, dir;
			camera.Ray(softwareCursor->GetPosition(), eye, dir);

			// The renderer centres voxel (x,y,z) at world (x,y,z), so traverse in a
			// +0.5-shifted space where each integer cell maps to a voxel index.
			Vector3 o = eye + MakeVector3(0.5F, 0.5F, 0.5F);

			int cx = int(std::floor(o.x));
			int cy = int(std::floor(o.y));
			int cz = int(std::floor(o.z));

			int stepX = (dir.x >= 0.0F) ? 1 : -1;
			int stepY = (dir.y >= 0.0F) ? 1 : -1;
			int stepZ = (dir.z >= 0.0F) ? 1 : -1;

			float big = 1.0e30F;
			float tDeltaX = (dir.x != 0.0F) ? std::fabs(1.0F / dir.x) : big;
			float tDeltaY = (dir.y != 0.0F) ? std::fabs(1.0F / dir.y) : big;
			float tDeltaZ = (dir.z != 0.0F) ? std::fabs(1.0F / dir.z) : big;

			float tMaxX = (dir.x != 0.0F)
			  ? ((float(cx) + (stepX > 0 ? 1.0F : 0.0F) - o.x) / dir.x) : big;
			float tMaxY = (dir.y != 0.0F)
			  ? ((float(cy) + (stepY > 0 ? 1.0F : 0.0F) - o.y) / dir.y) : big;
			float tMaxZ = (dir.z != 0.0F)
			  ? ((float(cz) + (stepZ > 0 ? 1.0F : 0.0F) - o.z) / dir.z) : big;

			int px = cx, py = cy, pz = cz;
			int limit = (model->GetWidth() + model->GetHeight() + model->GetDepth()) * 3 + 8;
			for (int i = 0; i < limit; i++) {
				if (InBounds(cx, cy, cz) && model->IsSolid(cx, cy, cz)) {
					pickHit = true;
					pickHX = cx; pickHY = cy; pickHZ = cz;
					pickPX = px; pickPY = py; pickPZ = pz;
					return;
				}
				px = cx; py = cy; pz = cz;
				if (tMaxX <= tMaxY && tMaxX <= tMaxZ) {
					cx += stepX; tMaxX += tDeltaX;
				} else if (tMaxY <= tMaxZ) {
					cy += stepY; tMaxY += tDeltaY;
				} else {
					cz += stepZ; tMaxZ += tDeltaZ;
				}
			}
		}

		int KV6EditorView::MirrorIdx(int i, float plane) const { return HalfSteps(plane) - i; }

		void KV6EditorView::ExpandMirrors(std::vector<IntVector3>& cells) const {
			const MirrorSetup& mirror = edit.mirror;
			const bool mx = MirrorOn(0), my = MirrorOn(1), mz = MirrorOn(2);
			if (mx || my || mz) {
				std::vector<IntVector3> out;
				out.reserve(cells.size() * 8);
				for (const IntVector3& c : cells) {
					int xs[2] = {c.x, c.x}, nx = 1;
					int ys[2] = {c.y, c.y}, ny = 1;
					int zs[2] = {c.z, c.z}, nz = 1;
					if (mx) { int m = MirrorIdx(c.x, mirror.plane.x); if (m != c.x) { xs[1] = m; nx = 2; } }
					if (my) { int m = MirrorIdx(c.y, mirror.plane.y); if (m != c.y) { ys[1] = m; ny = 2; } }
					if (mz) { int m = MirrorIdx(c.z, mirror.plane.z); if (m != c.z) { zs[1] = m; nz = 2; } }
					for (int a = 0; a < nx; a++)
					for (int b = 0; b < ny; b++)
					for (int d = 0; d < nz; d++)
						out.push_back(MakeIntVector3(xs[a], ys[b], zs[d]));
				}
				cells.swap(out);
			}
			// A cell and its image can both be among the cells (a box across a
			// plane): each is listed once, so counts of what an edit touches hold.
			auto before = [](const IntVector3& a, const IntVector3& b) {
				return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
			};
			std::sort(cells.begin(), cells.end(), before);
			cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
		}

		void KV6EditorView::RebuildVolume(int nw, int nh, int nd, int ox, int oy, int oz) {
			undo.RecordReframe(model->GetWidth(), model->GetHeight(), model->GetDepth(), nw, nh, nd,
			                   ox, oy, oz);
			ReframeRaw(nw, nh, nd, ox, oy, oz);
		}

		void KV6EditorView::ReframeRaw(int nw, int nh, int nd, int ox, int oy, int oz) {
			Handle<VoxelModel> dst = Handle<VoxelModel>::New(nw, nh, nd);
			for (int x = 0; x < model->GetWidth(); x++)
			for (int y = 0; y < model->GetHeight(); y++)
			for (int z = 0; z < model->GetDepth(); z++) {
				if (model->IsSolid(x, y, z))
					dst->SetSolid(x + ox, y + oy, z + oz, model->GetColor(x, y, z));
			}
			Vector3 shift = MakeVector3(float(ox), float(oy), float(oz));
			dst->SetOrigin(model->GetOrigin() - shift);
			model = dst;
			InvalidateRenderModel();
			orbitTarget += shift;
			cubeSize = std::max(nw, std::max(nh, nd));
			// Everything else held in voxel coordinates follows the relabelling:
			// the mirror planes (by whole voxels, so still on the half-step
			// grid), the selection, and pending voxels, which would otherwise
			// land in the wrong place.
			const IntVector3 offset = MakeIntVector3(ox, oy, oz);
			edit.mirror.plane += shift;
			edit.selection.Shift(offset);
			if (edit.placing) {
				PendingPlacement& pending = edit.placement;
				pending.anchor = pending.anchor + offset;
				pending.pivot = pending.pivot + offset;
				if (!pending.lifted->empty()) {
					for (IntVector3& v : pending.lifted.Edit())
						v = v + offset;
				}
			}
		}

		void KV6EditorView::TrimVolume() {
			int minX = model->GetWidth(), minY = model->GetHeight(), minZ = model->GetDepth();
			int maxX = -1, maxY = -1, maxZ = -1;
			for (int x = 0; x < model->GetWidth(); x++)
			for (int y = 0; y < model->GetHeight(); y++)
			for (int z = 0; z < model->GetDepth(); z++) {
				if (!model->IsSolid(x, y, z))
					continue;
				minX = std::min(minX, x); minY = std::min(minY, y); minZ = std::min(minZ, z);
				maxX = std::max(maxX, x); maxY = std::max(maxY, y); maxZ = std::max(maxZ, z);
			}
			if (maxX < 0)
				return;
			int nw = maxX - minX + 1, nh = maxY - minY + 1, nd = maxZ - minZ + 1;
			if (nw == model->GetWidth() && nh == model->GetHeight() && nd == model->GetDepth())
				return;
			RebuildVolume(nw, nh, nd, -minX, -minY, -minZ);
		}

		void KV6EditorView::PlaceCube() {
			DocumentCommand command(*this); // pending voxels land first, then the pick sees them
			DoPick();
			if (pickHit)
				Fill(std::vector<IntVector3>(1, PickPlace()), currentColor, "Place");
		}

		void KV6EditorView::DeleteCube() {
			DocumentCommand command(*this);
			DoPick();
			if (pickHit)
				Erase(std::vector<IntVector3>(1, PickSolid()), "Delete");
		}

		KV6EditorView::VolumeFrame KV6EditorView::FrameHolding(const IntVector3& lo,
		                                                       const IntVector3& hi) const {
			const IntVector3 from =
			  MakeIntVector3(std::min(0, lo.x), std::min(0, lo.y), std::min(0, lo.z));
			const IntVector3 to = MakeIntVector3(std::max(model->GetWidth(), hi.x + 1),
			                                     std::max(model->GetHeight(), hi.y + 1),
			                                     std::max(model->GetDepth(), hi.z + 1));
			VolumeFrame frame;
			frame.size = to - from;
			frame.shift = MakeIntVector3(-from.x, -from.y, -from.z);
			return frame;
		}

		bool KV6EditorView::VolumeFrame::Fits() const {
			return FitsModelSize(size.x, size.y, size.z);
		}

		void KV6EditorView::Reframe(const VolumeFrame& frame) {
			if (frame.shift == MakeIntVector3(0, 0, 0) &&
			    frame.size == MakeIntVector3(model->GetWidth(), model->GetHeight(), model->GetDepth()))
				return;
			RebuildVolume(frame.size.x, frame.size.y, frame.size.z, frame.shift.x, frame.shift.y,
			              frame.shift.z);
		}

		void KV6EditorView::Fill(std::vector<IntVector3> cells, uint32_t color,
		                         const std::string& label) {
			DocumentCommand command(*this);
			ExpandMirrors(cells); // also fill the mirror images, if enabled
			IntVector3 lo, hi;
			if (!BoundsOf(cells, lo, hi))
				return;
			// The cells may lie past the volume: grow it to hold them all.
			const VolumeFrame frame = FrameHolding(lo, hi);
			if (!frame.Fits()) {
				SetStatus(kMaxModelSizeMessage);
				return;
			}
			KV6UndoStack::Step step(undo, label);
			Reframe(frame);
			bool wrote = false;
			for (const IntVector3& c : cells) {
				const IntVector3 at = c + frame.shift;
				if (InBounds(at.x, at.y, at.z) && !model->IsSolid(at.x, at.y, at.z)) {
					WriteVoxel(at.x, at.y, at.z, true, color);
					wrote = true;
				}
			}
			if (wrote)
				NoteColorUsed(color);
		}

		void KV6EditorView::Erase(std::vector<IntVector3> cells, const std::string& label) {
			DocumentCommand command(*this);
			ExpandMirrors(cells); // also erase the mirror images, if enabled
			int count = 0;
			for (const IntVector3& c : cells) {
				if (InBounds(c.x, c.y, c.z) && model->IsSolid(c.x, c.y, c.z))
					count++;
			}
			if (count == 0)
				return;
			if (voxelCount - count < 1) {
				SetStatus(kLastVoxelMessage);
				return;
			}
			KV6UndoStack::Step step(undo, label);
			for (const IntVector3& c : cells) {
				if (InBounds(c.x, c.y, c.z) && model->IsSolid(c.x, c.y, c.z))
					WriteVoxel(c.x, c.y, c.z, false, 0);
			}
			TrimVolume();
		}

		// --- Colour -----------------------------------------------------------

		Vector4 KV6EditorView::ColorToVec(uint32_t c) const {
			return ConvertColorRGBA(IntVectorFromColor(c));
		}

		bool KV6EditorView::SamplingArmed() const {
			return altHeld || ui->GetColorPicker()->GetEyedropperMode();
		}

		bool KV6EditorView::SampleColor() {
			DoPick();
			if (!pickHit)
				return false;
			currentColor = model->GetColor(pickHX, pickHY, pickHZ) & 0xFFFFFF;
			ColorPicker& picker = *ui->GetColorPicker();
			picker.SetColor(currentColor);
			picker.SetEyedropperMode(false); // a pick is one-shot
			SetStatus("Picked colour");
			return true;
		}

		// --- Selection --------------------------------------------------------

		bool KV6EditorView::IsSelected(int x, int y, int z) const {
			return edit.selection.Contains(MakeIntVector3(x, y, z));
		}

		void KV6EditorView::ClearSelection() {
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Clear Selection");
			edit.selection.Clear();
		}

		std::vector<IntVector3> KV6EditorView::LinkedColorRegion(int x, int y, int z) const {
			std::vector<IntVector3> region;
			if (!InBounds(x, y, z) || !model->IsSolid(x, y, z))
				return region;
			const uint32_t target = model->GetColor(x, y, z) & 0xFFFFFF;
			auto linked = [&](const IntVector3& v) {
				return InBounds(v.x, v.y, v.z) && model->IsSolid(v.x, v.y, v.z) &&
				       (model->GetColor(v.x, v.y, v.z) & 0xFFFFFF) == target;
			};
			// Every cell is queued once: `queued` holds all that ever were.
			VoxelSelection queued;
			std::vector<IntVector3> stack(1, MakeIntVector3(x, y, z));
			queued.Add(stack.back());
			while (!stack.empty()) {
				const IntVector3 c = stack.back();
				stack.pop_back();
				region.push_back(c);
				for (const IntVector3& step : kFaceNeighbours) {
					const IntVector3 n = c + step;
					if (!queued.Contains(n) && linked(n)) {
						queued.Add(n);
						stack.push_back(n);
					}
				}
			}
			return region;
		}

		void KV6EditorView::DrawSelection() {
			const Vector4 col = MakeVector4(1.0F, 0.55F, 0.1F, 0.95F);
			edit.selection.ForEach(
			  [&](const IntVector3& v) { DrawCellOutline(v.x, v.y, v.z, col); });
		}

		// --- Clipboard / paste -----------------------------------------------
		//
		// Copy, Cut and Delete act on the selection. While voxels are pending,
		// they are what is selected (lifted voxels were the selection, a paste
		// or an import is what the user is handling), so the commands act on
		// them where they are, and never place them first.

		void KV6EditorView::CopySelection() {
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			std::vector<ClipVoxel> copied;
			IntVector3 lo, hi;
			if (edit.placing) {
				copied = *edit.placement.voxels; // already relative to their box
			} else if (edit.selection.Bounds(lo, hi)) {
				copied.reserve(size_t(edit.selection.Size()));
				edit.selection.ForEach([&](const IntVector3& v) {
					copied.push_back({v - lo, model->GetColor(v.x, v.y, v.z) & 0xFFFFFF});
				});
			}
			if (copied.empty()) {
				SetStatus("Nothing selected");
				return;
			}
			clipboard = std::move(copied);
			SetStatus("Copied " + std::to_string(clipboard.size()) + " voxels");
		}

		bool KV6EditorView::CutSelection() {
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			if (!CanEraseSelection())
				return false;
			CopySelection();
			SetStatus("Cut " + std::to_string(EraseSelection("Cut")) + " voxels");
			return true;
		}

		void KV6EditorView::DeleteSelection() {
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			if (!CanEraseSelection())
				return;
			SetStatus("Deleted " + std::to_string(EraseSelection("Delete")) + " voxels");
		}

		bool KV6EditorView::CanEraseSelection() {
			const int count =
			  edit.placing ? int(edit.placement.voxels->size()) : edit.selection.Size();
			if (count == 0) {
				SetStatus("Nothing selected");
				return false;
			}
			// Pending voxels are out of the document already: dropping them
			// leaves what it holds now.
			const int remaining = edit.placing ? voxelCount : voxelCount - count;
			if (remaining < 1) {
				SetStatus(kLastVoxelMessage);
				return false;
			}
			return true;
		}

		int KV6EditorView::EraseSelection(const std::string& label) {
			KV6UndoStack::Step step(undo, label);
			if (edit.placing) {
				// A paste or an import vanishes; lifted voxels stay out of the
				// document, where lifting already took them from.
				const int erased = int(edit.placement.voxels->size());
				DropPlacement();
				TrimVolume();
				return erased;
			}
			const VoxelSelection selected = edit.selection;
			edit.selection.Clear();
			selected.ForEach([&](const IntVector3& v) { WriteVoxel(v.x, v.y, v.z, false, 0); });
			TrimVolume();
			return selected.Size();
		}

		void KV6EditorView::Paste() {
			if (clipboard.empty()) {
				SetStatus("Clipboard is empty");
				return;
			}
			IntVector3 anchor = MakeIntVector3(model->GetWidth() / 2, model->GetHeight() / 2,
			                                   model->GetDepth() / 2);
			StartPlacement(clipboard, "Paste", anchor);
		}

		void KV6EditorView::StartPlacement(std::vector<ClipVoxel> voxels, const std::string& label,
		                                   const IntVector3& anchor) {
			if (voxels.empty())
				return;
			// Anything already pending belongs to the document before this starts.
			DocumentCommand command(*this);

			IntVector3 at = anchor;
			if (!ClampPlacementAnchor(ExtentOf(voxels), at)) {
				SetStatus(label + ": too large for the maximum model size");
				return;
			}
			{
				KV6UndoStack::Step step(undo, label);
				edit.placement = MakePlacement(std::move(voxels), at, std::vector<IntVector3>(), label);
				edit.placing = true;
			}

			// Positioning a placement is what the Transform tool is for, so go there.
			if (!ActivateTransformTool()) {
				SetStatus(label + ": could not open the Transform tool");
				return;
			}
			SetStatus(label + ": position it with the gizmo, then click away to place it");
		}

		bool KV6EditorView::ActivateTransformTool() {
			// Matched by type rather than by label, so neither renaming a button nor
			// another sub-tool taking the same label can send a placement elsewhere.
			for (size_t i = 0; i < tools.size(); i++) {
				EditorTool& tool = *tools[i].tool;
				for (int sub = 0; sub < tool.SubToolCount(); sub++) {
					if (!dynamic_cast<TransformSubTool*>(tool.SubTool(sub)))
						continue;
					SetMode(EditorMode::Edit);
					SetActiveTool(int(i));
					tool.SetSubTool(*this, sub);
					return true;
				}
			}
			return false;
		}

		void KV6EditorView::ImportModel(const std::string& path) {
			Handle<VoxelModel> imported(io->Load(path), false); // adopt (Load returns a ref)
			if (!imported) {
				SetStatus("Could not load " + path);
				return;
			}

			// Collect the solid voxels relative to their own min corner. Interior
			// voxels keep the sentinel colour the loader fills them with, exactly as
			// in a document opened from disk; the writer drops them again on save.
			int minX = imported->GetWidth(), minY = imported->GetHeight(),
			    minZ = imported->GetDepth();
			int maxX = -1, maxY = -1, maxZ = -1;
			for (int x = 0; x < imported->GetWidth(); x++)
			for (int y = 0; y < imported->GetHeight(); y++)
			for (int z = 0; z < imported->GetDepth(); z++) {
				if (!imported->IsSolid(x, y, z))
					continue;
				minX = std::min(minX, x); maxX = std::max(maxX, x);
				minY = std::min(minY, y); maxY = std::max(maxY, y);
				minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
			}
			if (maxX < 0) {
				SetStatus("That model is empty");
				return;
			}

			std::vector<ClipVoxel> voxels;
			for (int x = minX; x <= maxX; x++)
			for (int y = minY; y <= maxY; y++)
			for (int z = minZ; z <= maxZ; z++) {
				if (imported->IsSolid(x, y, z))
					voxels.push_back({MakeIntVector3(x - minX, y - minY, z - minZ),
					                  imported->GetColor(x, y, z) & 0xFFFFFF});
			}

			// Start with the imported pivot on this document's pivot, which puts a
			// part (a hand, a barrel) where it belongs before any dragging.
			Vector3 importedPivot = imported->GetOrigin() * -1.0F;
			Vector3 aligned =
			  MakeVector3(float(minX), float(minY), float(minZ)) + GetPivot() - importedPivot;
			IntVector3 anchor = MakeIntVector3(int(std::floor(aligned.x + 0.5F)),
			                                   int(std::floor(aligned.y + 0.5F)),
			                                   int(std::floor(aligned.z + 0.5F)));
			StartPlacement(std::move(voxels), "Import", anchor);
		}

		bool KV6EditorView::PlacementFromSelection(PendingPlacement& out) const {
			IntVector3 lo, hi;
			if (!edit.selection.Bounds(lo, hi))
				return false;
			std::vector<IntVector3> lifted;
			std::vector<ClipVoxel> voxels;
			lifted.reserve(size_t(edit.selection.Size()));
			voxels.reserve(size_t(edit.selection.Size()));
			edit.selection.ForEach([&](const IntVector3& v) {
				lifted.push_back(v);
				voxels.push_back({v - lo, model->GetColor(v.x, v.y, v.z) & 0xFFFFFF});
			});
			out = MakePlacement(std::move(voxels), lo, std::move(lifted), "Transform");
			return true;
		}

		void KV6EditorView::LiftIntoPlacement(PendingPlacement taken) {
			// The voxels leave the document, journaled, so the gap they came from
			// shows while they are positioned and undo puts them back. Erasing
			// them also empties the selection: they are what is selected now.
			for (const IntVector3& v : *taken.lifted) {
				if (InBounds(v.x, v.y, v.z))
					WriteVoxel(v.x, v.y, v.z, false, 0);
			}
			edit.placement = std::move(taken);
			edit.placing = true;
		}

		bool KV6EditorView::LandVoxel(const IntVector3& at, uint32_t color) {
			if (!InBounds(at.x, at.y, at.z))
				return false;
			WriteVoxel(at.x, at.y, at.z, true, color);
			edit.selection.Add(at);
			return true;
		}

		KV6EditorView::DocumentCommand::DocumentCommand(KV6EditorView& editor, Pending pending)
		    : editor(editor) {
			// Counted only once the preparation is done: were it to throw, this
			// scope never existed and later commands must still see depth 0.
			if (editor.documentCommandDepth == 0) {
				editor.EndPreviews();
				if (pending == Pending::Apply)
					editor.ApplyPlacement();
			}
			editor.documentCommandDepth++;
		}

		KV6EditorView::DocumentCommand::~DocumentCommand() {
			if (--editor.documentCommandDepth > 0)
				return;
			// Whatever the command got done, even if it threw part way, the tool
			// catches up with it. Nothing may leave a destructor, so a tool that
			// fails to is logged instead.
			try {
				editor.NotifyDocumentChanged();
			} catch (const std::exception& ex) {
				SPLog("Editor tool failed to follow a document change: %s", ex.what());
			} catch (...) {
				SPLog("Editor tool failed to follow a document change");
			}
		}

		void KV6EditorView::DropPlacement() {
			edit.placing = false;
			edit.placement = PendingPlacement();
		}

		PendingPlacement KV6EditorView::MakePlacement(std::vector<ClipVoxel> voxels,
		                                              const IntVector3& anchor,
		                                              std::vector<IntVector3> lifted,
		                                              const std::string& label) {
			PendingPlacement p;
			p.voxels = CopyOnWrite<std::vector<ClipVoxel>>(std::move(voxels));
			if (!lifted.empty())
				p.lifted = CopyOnWrite<std::vector<IntVector3>>(std::move(lifted));
			p.anchor = anchor;
			// A turn about their middle keeps them about where they were.
			p.pivot = MiddleOf(anchor, anchor + p.Extent() - MakeIntVector3(1, 1, 1));
			p.label = label;
			return p;
		}

		bool KV6EditorView::TransformedPlacement(const PendingPlacement& from,
		                                         const PlacementTransform& t,
		                                         PendingPlacement& out, bool& clamped) const {
			out = from;
			clamped = false;

			if (t.Turns()) {
				// A quarter turn takes the axis after `t.axis` onto the one after
				// that, which is the right-handed sense about `t.axis`. Offsets from
				// a whole-voxel centre stay whole, so every voxel lands on a voxel.
				const int turns = ((t.quarterTurns % 4) + 4) % 4;
				const int u = (t.axis + 1) % 3, v = (t.axis + 2) % 3;
				const IntVector3 centre = TurnCentre(from.pivot);
				auto turned = [&](const IntVector3& p) {
					const IntVector3 d = p - centre;
					int c[3] = {d.x, d.y, d.z};
					for (int k = 0; k < turns; k++) {
						const int cu = c[u];
						c[u] = -c[v];
						c[v] = cu;
					}
					return centre + MakeIntVector3(c[0], c[1], c[2]);
				};
				// A turn is the one change that rewrites the voxels themselves.
				std::vector<ClipVoxel>& voxels = out.voxels.Edit();
				std::vector<IntVector3> cells(voxels.size());
				for (size_t i = 0; i < voxels.size(); i++)
					cells[i] = turned(from.anchor + voxels[i].rel);
				IntVector3 lo, hi;
				BoundsOf(cells, lo, hi);
				out.anchor = lo;
				for (size_t i = 0; i < voxels.size(); i++)
					voxels[i].rel = cells[i] - lo;
				// Their middle turns with them, so it is still their middle
				// whichever centre they turned about.
				out.pivot = turned(from.pivot);
			}

			out.anchor = out.anchor + t.shift;
			out.pivot = out.pivot + t.shift;
			IntVector3 fitted = out.anchor;
			if (!ClampPlacementAnchor(out.Extent(), fitted))
				return false;
			clamped = !(fitted == out.anchor);
			// The pivot stays with the voxels, so turning back still returns them.
			out.pivot = out.pivot + (fitted - out.anchor);
			out.anchor = fitted;
			return true;
		}

		void KV6EditorView::TransformPlacement(const PlacementTransform& t) {
			if (!t.IsValid() || t.IsIdentity())
				return;
			// Nothing pending yet: the move takes the selected voxels with it.
			PendingPlacement selected;
			const bool lifting = !edit.placing;
			if (lifting && !PlacementFromSelection(selected))
				return;

			PendingPlacement next;
			bool clamped = false;
			if (!TransformedPlacement(lifting ? selected : edit.placement, t, next, clamped)) {
				SetStatus(kMaxModelSizeMessage);
				return;
			}
			if (clamped)
				SetStatus(kMaxModelSizeMessage); // went as far as the limit allows

			{
				KV6UndoStack::Step step(undo, t.Turns() ? "Turn" : "Move");
				if (lifting)
					LiftIntoPlacement(std::move(selected));
				edit.placement = std::move(next);
			}
		}

		bool KV6EditorView::TransformPivot(IntVector3& out) const {
			// From what is known already (the pending voxels' middle, the
			// selection's cached box): it is asked for every frame.
			if (edit.placing) {
				out = TurnCentre(edit.placement.pivot);
				return true;
			}
			IntVector3 lo, hi;
			if (!edit.selection.Bounds(lo, hi))
				return false;
			out = TurnCentre(MiddleOf(lo, hi)); // the pivot lifting it would give
			return true;
		}

		IntVector3 KV6EditorView::TurnCentre(const IntVector3& groupMiddle) const {
			if (!turnsAboutModelPivot)
				return groupMiddle;
			// The voxel nearest the pivot; on a tie the lower one, as for the
			// middle of a group.
			const Vector3 p = GetPivot();
			auto nearest = [](float c) { return int(std::ceil(c - 0.5F)); };
			return MakeIntVector3(nearest(p.x), nearest(p.y), nearest(p.z));
		}

		bool KV6EditorView::ClampPlacementAnchor(const IntVector3& box, IntVector3& anchor) const {
			const int extent[3] = {box.x, box.y, box.z};
			const int size[3] = {model->GetWidth(), model->GetHeight(), model->GetDepth()};
			const int limit[3] = {kMaxModelWidth, kMaxModelHeight, kMaxModelDepth};
			int at[3] = {anchor.x, anchor.y, anchor.z};
			for (int a = 0; a < 3; a++) {
				if (extent[a] > limit[a] || size[a] > limit[a])
					return false;
				// Landing grows the volume to span min(0, at) .. max(size, at + extent),
				// which stays within the limit exactly for `at` in this range.
				at[a] = std::max(size[a] - limit[a], std::min(limit[a] - extent[a], at[a]));
			}
			anchor = MakeIntVector3(at[0], at[1], at[2]);
			return true;
		}

		void KV6EditorView::ApplyPlacement() {
			if (!edit.placing)
				return;
			const PendingPlacement pending = edit.placement;

			// Every placement is kept where it fits (ClampPlacementAnchor), so the
			// volume holding it does; were it ever not to, putting the voxels back
			// would lose nothing.
			const VolumeFrame frame = FrameHolding(
			  pending.anchor, pending.anchor + pending.Extent() - MakeIntVector3(1, 1, 1));
			if (!frame.Fits()) {
				CancelPlacement();
				SetStatus(kMaxModelSizeMessage);
				return;
			}

			int placed = 0;
			{
				KV6UndoStack::Step step(undo, pending.label);
				// No longer pending before the volume changes, so the relabelling
				// below leaves `pending` in the frame it was measured in.
				DropPlacement();
				Reframe(frame);
				edit.selection.Clear(); // what was just placed is what is selected
				for (const ClipVoxel& v : *pending.voxels) {
					if (LandVoxel(pending.anchor + v.rel + frame.shift, v.color))
						placed++;
				}
				TrimVolume();
			}
			SetStatus(pending.label + ": placed " + std::to_string(placed) + " voxels");
		}

		void KV6EditorView::CancelPlacement() {
			if (!edit.placing)
				return;
			const PendingPlacement pending = edit.placement;
			{
				KV6UndoStack::Step step(undo, "Cancel " + pending.label);
				DropPlacement();
				// Lifted voxels go back where they came from, selected as they were;
				// `lifted` and `voxels` are parallel, so each keeps its colour.
				const std::vector<IntVector3>& lifted = *pending.lifted;
				for (size_t i = 0; i < lifted.size(); i++)
					LandVoxel(lifted[i], (*pending.voxels)[i].color);
			}
			SetStatus(pending.label + " cancelled");
		}

		void KV6EditorView::DrawPlacementPreview() {
			// The voxels themselves are rendered solid (see RunFrame); outline the
			// volume so it reads as pending rather than part of the document.
			if (!edit.placing)
				return;
			const PendingPlacement& pending = edit.placement;
			DrawBoxOutline(pending.anchor, pending.anchor + pending.Extent() - MakeIntVector3(1, 1, 1),
			               MakeVector4(0.4F, 1.0F, 0.5F, 0.9F));
		}

		void KV6EditorView::DrawPlacementTransformed(const PlacementTransform& t,
		                                             const Vector4& color) {
			if (!t.IsValid())
				return;
			PendingPlacement selected;
			if (!edit.placing && !PlacementFromSelection(selected))
				return;
			PendingPlacement preview;
			bool clamped = false;
			if (!TransformedPlacement(edit.placing ? edit.placement : selected, t, preview, clamped))
				return;
			for (const ClipVoxel& v : *preview.voxels) {
				const IntVector3 at = preview.anchor + v.rel;
				DrawCellOutline(at.x, at.y, at.z, color);
			}
		}

		// --- Layout / hit testing --------------------------------------------

		bool KV6EditorView::InRect(const Vector2& p, float x, float y, float w, float h) const {
			return OverlayInRect(p, x, y, w, h);
		}

		// --- Drawing primitives ----------------------------------------------

		void KV6EditorView::ColorNP(const Vector4& c) { OverlayColorNP(*renderer, c); }
		void KV6EditorView::FillRect(float x, float y, float w, float h) {
			OverlayFillRect(*renderer, x, y, w, h);
		}
		void KV6EditorView::StrokeRect(float x, float y, float w, float h, float t,
		                               const Vector4& c) {
			OverlayStrokeRect(*renderer, x, y, w, h, t, c);
		}
		void KV6EditorView::DrawLine2D(const Vector2& a, const Vector2& b, float w,
		                               const Vector4& col) {
			OverlayStrokeLine(*renderer, a, b, w, col);
		}

		void KV6EditorView::DrawCellOutline(int x, int y, int z, const Vector4& color) {
			Vector3 a = MakeVector3(float(x) - 0.5F, float(y) - 0.5F, float(z) - 0.5F);
			Vector3 b = MakeVector3(float(x) + 0.5F, float(y) + 0.5F, float(z) + 0.5F);
			BoxEdges(a, b, [&](const Vector3& p, const Vector3& q) { EmitLine(p, q, color); });
		}

		void KV6EditorView::DrawBoxOutline(const IntVector3& lo, const IntVector3& hi,
		                                   const Vector4& color) {
			Vector3 a = MakeVector3(float(lo.x) - 0.5F, float(lo.y) - 0.5F, float(lo.z) - 0.5F);
			Vector3 b = MakeVector3(float(hi.x) + 0.5F, float(hi.y) + 0.5F, float(hi.z) + 0.5F);
			BoxEdges(a, b, [&](const Vector3& p, const Vector3& q) { EmitLine(p, q, color); });
		}

		void KV6EditorView::DrawCellOutlineMirrored(int x, int y, int z, const Vector4& color) {
			std::vector<IntVector3> cells;
			cells.push_back(MakeIntVector3(x, y, z));
			ExpandMirrors(cells);
			for (const IntVector3& c : cells)
				DrawCellOutline(c.x, c.y, c.z, color);
		}

		void KV6EditorView::DrawBoxOutlineMirrored(const IntVector3& lo, const IntVector3& hi,
		                                           const Vector4& color) {
			for (int mx = 0; mx <= (MirrorOn(0) ? 1 : 0); mx++)
			for (int my = 0; my <= (MirrorOn(1) ? 1 : 0); my++)
			for (int mz = 0; mz <= (MirrorOn(2) ? 1 : 0); mz++) {
				IntVector3 a = lo, b = hi;
				if (mx) { int p = MirrorIdx(lo.x, edit.mirror.plane.x), q = MirrorIdx(hi.x, edit.mirror.plane.x);
				          a.x = std::min(p, q); b.x = std::max(p, q); }
				if (my) { int p = MirrorIdx(lo.y, edit.mirror.plane.y), q = MirrorIdx(hi.y, edit.mirror.plane.y);
				          a.y = std::min(p, q); b.y = std::max(p, q); }
				if (mz) { int p = MirrorIdx(lo.z, edit.mirror.plane.z), q = MirrorIdx(hi.z, edit.mirror.plane.z);
				          a.z = std::min(p, q); b.z = std::max(p, q); }
				DrawBoxOutline(a, b, color);
			}
		}

		void KV6EditorView::SelectIfSolid(const IntVector3& v) {
			if (InBounds(v.x, v.y, v.z) && model->IsSolid(v.x, v.y, v.z))
				edit.selection.Add(v);
		}

		void KV6EditorView::SelectBox(const IntVector3& lo, const IntVector3& hi) {
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Select");
			for (int x = lo.x; x <= hi.x; x++)
			for (int y = lo.y; y <= hi.y; y++)
			for (int z = lo.z; z <= hi.z; z++)
				SelectIfSolid(MakeIntVector3(x, y, z));
		}

		void KV6EditorView::SelectCells(const std::vector<IntVector3>& cells) {
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Select");
			for (const IntVector3& c : cells)
				SelectIfSolid(c);
		}

		void KV6EditorView::FillCells(const std::vector<IntVector3>& cells, uint32_t color) {
			Fill(cells, color, "Fill");
		}

		void KV6EditorView::EraseCells(const std::vector<IntVector3>& cells) {
			Erase(cells, "Erase");
		}

		void KV6EditorView::DeselectCells(const std::vector<IntVector3>& cells) {
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Deselect");
			for (const IntVector3& c : cells)
				edit.selection.Remove(c);
		}

		void KV6EditorView::PaintCells(const std::vector<IntVector3>& cellsIn, uint32_t color) {
			DocumentCommand command(*this);
			std::vector<IntVector3> cells = cellsIn;
			ExpandMirrors(cells); // also recolour the mirror images, if enabled
			uint32_t rgb = color & 0xFFFFFF;
			KV6UndoStack::Step step(undo, "Paint");
			bool wrote = false;
			for (const IntVector3& c : cells) {
				if (!InBounds(c.x, c.y, c.z) || !model->IsSolid(c.x, c.y, c.z))
					continue; // paint only existing voxels; never grows the volume
				if ((model->GetColor(c.x, c.y, c.z) & 0xFFFFFF) != rgb) {
					WriteVoxel(c.x, c.y, c.z, true, rgb);
					wrote = true;
				}
			}
			if (wrote)
				NoteColorUsed(rgb);
		}

		void KV6EditorView::NoteColorUsed(uint32_t color) {
			ui->GetColorPicker()->AddRecentColor(color);
		}

		void KV6EditorView::ApplyCells(const std::vector<IntVector3>& cells, bool secondary) {
			EditorTool* t = ActiveTool();
			EditorRole role = t ? t->Role() : EditorRole::Edit;
			if (role == EditorRole::Select) {
				if (secondary) DeselectCells(cells);
				else SelectCells(cells);
			} else if (role == EditorRole::Paint) {
				if (!secondary) // recolouring has no inverse
					PaintCells(cells, currentColor);
			} else {
				if (secondary) EraseCells(cells);
				else FillCells(cells, currentColor);
			}
		}

		// --- Undo / redo ------------------------------------------------------

		// The one place voxels are written: keeps `voxelCount` correct and (unlike
		// WriteVoxelRaw) journals the change so it can be undone.
		void KV6EditorView::WriteVoxel(int x, int y, int z, bool solid, uint32_t color) {
			if (!InBounds(x, y, z))
				return;
			if (!solid)
				edit.selection.Remove(MakeIntVector3(x, y, z)); // it holds solid voxels only
			bool oldSolid = model->IsSolid(x, y, z);
			uint32_t oldColor = model->GetColor(x, y, z) & 0xFFFFFF;
			uint32_t newColor = solid ? (color & 0xFFFFFF) : oldColor; // air keeps its colour
			WriteVoxelRaw(x, y, z, solid, newColor);
			undo.RecordVoxel(x, y, z, oldSolid, oldColor, solid, newColor);
		}

		void KV6EditorView::WriteVoxelRaw(int x, int y, int z, bool solid, uint32_t color) {
			InvalidateRenderModel();
			bool was = model->IsSolid(x, y, z);
			if (solid)
				model->SetSolid(x, y, z, color);
			else
				model->SetAir(x, y, z);
			if (was && !solid)
				voxelCount--;
			else if (!was && solid)
				voxelCount++;
		}

		// The live state is an EditState, so a snapshot is a copy of it: cheap,
		// as its large parts are shared until changed.
		EditState KV6EditorView::UndoSnapshotState() const { return edit; }

		void KV6EditorView::UndoRestoreState(const EditState& state) { edit = state; }

		// KV6UndoStack::Sink — the stack replays records through these.
		void KV6EditorView::UndoApplyVoxel(int x, int y, int z, bool solid, uint32_t color) {
			WriteVoxelRaw(x, y, z, solid, color);
		}
		void KV6EditorView::UndoApplyReframe(int w, int h, int d, int ox, int oy, int oz) {
			ReframeRaw(w, h, d, ox, oy, oz);
		}

		// Undo and redo replay the one history, pending voxels included: a
		// move or a turn is a step like any edit, so nothing is placed first.
		void KV6EditorView::Undo() {
			EndPreviews(); // the history holds only what was committed
			std::string label = undo.UndoLabel();
			if (!undo.Undo()) {
				SetStatus("Nothing to undo");
				return;
			}
			SetStatus("Undid " + (label.empty() ? std::string("edit") : label));
			HistoryReplayed();
		}
		void KV6EditorView::Redo() {
			EndPreviews(); // the history holds only what was committed
			std::string label = undo.RedoLabel();
			if (!undo.Redo()) {
				SetStatus("Nothing to redo");
				return;
			}
			SetStatus("Redid " + (label.empty() ? std::string("edit") : label));
			HistoryReplayed();
		}

		void KV6EditorView::HistoryReplayed() {
			if (edit.placing)
				ActivateTransformTool();
			NotifyDocumentChanged();
		}

		// --- Pivot ------------------------------------------------------------

		Vector3 KV6EditorView::GetPivot() const { return model->GetOrigin() * -1.0F; }

		// The renderer bakes `origin` into the render model, so re-bake after changing
		// it; that keeps the voxels visually fixed while the pivot marker moves.
		void KV6EditorView::ApplyOriginRaw(const Vector3& origin) noexcept {
			model->SetOrigin(origin);
			InvalidateRenderModel();
		}

		void KV6EditorView::SetPivot(const Vector3& pivot) {
			DocumentCommand command(*this);
			Vector3 before = model->GetOrigin();
			Vector3 after = pivot * -1.0F;
			if (after.x == before.x && after.y == before.y && after.z == before.z)
				return;
			KV6UndoStack::Step step(undo, "Set Pivot");
			ApplyOriginRaw(after);
			undo.RecordOrigin(before, after);
		}

		void KV6EditorView::UndoApplyOrigin(const Vector3& origin) { ApplyOriginRaw(origin); }

		// Live, non-journaled pivot move for a drag in progress; the tool commits the
		// net change with one SetPivot on release.
		void KV6EditorView::PreviewPivot(const Vector3& pivot) {
			if (!previewedOrigin)
				previewedOrigin = model->GetOrigin();
			ApplyOriginRaw(pivot * -1.0F);
		}

		void KV6EditorView::BeginPivotEntry() {
			Vector3 p = GetPivot();
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%.1f %.1f %.1f", p.x, p.y, p.z);
			ui->GetEditorMenu()->OpenTextPrompt("Set pivot (x y z)", buf, [this](const std::string& s) {
				std::string str = s;
				for (char& c : str)
					if (c == ',')
						c = ' ';
				float x, y, z;
				if (std::sscanf(str.c_str(), "%f %f %f", &x, &y, &z) == 3) {
					SetPivot(MakeVector3(x, y, z));
					SetStatus("Pivot set");
				} else {
					SetStatus("Enter three numbers: x y z");
				}
			});
		}

		// Long world axes through the model's origin (pivot), which renders at
		// world coordinate -origin in the editor's grid space.
		void KV6EditorView::DrawOriginAxes() {
			Vector3 org = model->GetOrigin();
			Vector3 p = MakeVector3(-org.x, -org.y, -org.z);
			float L = 1000.0F;
			renderer->AddDebugLine(p - MakeVector3(L, 0, 0), p + MakeVector3(L, 0, 0),
			                       MakeVector4(1.0F, 0.3F, 0.3F, 0.5F));
			renderer->AddDebugLine(p - MakeVector3(0, L, 0), p + MakeVector3(0, L, 0),
			                       MakeVector4(0.4F, 1.0F, 0.4F, 0.5F));
			renderer->AddDebugLine(p - MakeVector3(0, 0, L), p + MakeVector3(0, 0, L),
			                       MakeVector4(0.45F, 0.6F, 1.0F, 0.5F));
		}

		// The ribbon + main toolbar + sub-toolbar are always present; the 3D
		// viewport sits below them.
		float KV6EditorView::BarsH() { return kBarsH; }

		void KV6EditorView::DrawHelpers() {
			// Opaque (alpha 1.0, so the lines cover what is behind them) but kept
			// close to the background tone: the floor should be legible without
			// competing with the model. Contrast comes from the minor/major
			// difference rather than from brightness.
			Vector4 grid = MakeVector4(0.19F, 0.20F, 0.23F, 1.0F);
			Vector4 gridMajor = MakeVector4(0.30F, 0.32F, 0.37F, 1.0F);
			Vector4 box = MakeVector4(0.4F, 0.7F, 1.0F, 0.5F);

			// A fixed grid in world space: it does not follow the model, the volume
			// or the camera, so it reads as the ground rather than something moving
			// with the subject.
			float z = kGroundPlaneZ;
			float from = float(-kGroundReach) - 0.5F; // voxel edges, not centres
			float to = float(kGroundReach) - 0.5F;
			for (int i = -kGroundReach; i <= kGroundReach; i += kGroundStep) {
				float at = float(i) - 0.5F;
				bool major = (i % (kGroundStep * kGroundMajorEvery)) == 0;
				const Vector4& color = major ? gridMajor : grid;
				renderer->AddDebugLine(MakeVector3(at, from, z), MakeVector3(at, to, z), color);
				renderer->AddDebugLine(MakeVector3(from, at, z), MakeVector3(to, at, z), color);
			}

			float lo = -0.5F;
			Vector3 a = MakeVector3(lo, lo, lo);
			Vector3 b = MakeVector3(float(model->GetWidth()) - 0.5F,
			                        float(model->GetHeight()) - 0.5F,
			                        float(model->GetDepth()) - 0.5F);
			BoxEdges(a, b, [&](const Vector3& p, const Vector3& q) {
				renderer->AddDebugLine(p, q, box);
			});
		}

		// Semi-transparent quad at each enabled mirror plane (drawn as a fan of
		// debug lines, since the editor only has line primitives in 3D).
		void KV6EditorView::DrawMirrorPlanes() {
			int w = model->GetWidth(), h = model->GetHeight(), d = model->GetDepth();

			// Grid lines are spaced one tile (2 voxels) apart. Extend one tile past
			// the model on every side, rounding the far edge up to the tile grid so
			// the plane is a whole number of tiles and always shows a complete border
			// (the model spans [-0.5, dim-0.5]; the near edge -2.5 is already aligned).
			auto stop = [](int n) { return (n % 2 == 0) ? n + 2 : n + 3; };
			float lo = -2.5F;
			float hiX = float(stop(w)) - 0.5F;
			float hiY = float(stop(h)) - 0.5F;
			float hiZ = float(stop(d)) - 0.5F;

			if (MirrorOn(0)) {
				float px = edit.mirror.plane.x;
				Vector4 col = MakeVector4(1.0F, 0.35F, 0.35F, 0.25F);
				for (int i = -2; i <= stop(h); i += 2)
					renderer->AddDebugLine(MakeVector3(px, float(i) - 0.5F, lo),
					                       MakeVector3(px, float(i) - 0.5F, hiZ), col);
				for (int i = -2; i <= stop(d); i += 2)
					renderer->AddDebugLine(MakeVector3(px, lo, float(i) - 0.5F),
					                       MakeVector3(px, hiY, float(i) - 0.5F), col);
			}
			if (MirrorOn(1)) {
				float py = edit.mirror.plane.y;
				Vector4 col = MakeVector4(0.4F, 1.0F, 0.4F, 0.25F);
				for (int i = -2; i <= stop(w); i += 2)
					renderer->AddDebugLine(MakeVector3(float(i) - 0.5F, py, lo),
					                       MakeVector3(float(i) - 0.5F, py, hiZ), col);
				for (int i = -2; i <= stop(d); i += 2)
					renderer->AddDebugLine(MakeVector3(lo, py, float(i) - 0.5F),
					                       MakeVector3(hiX, py, float(i) - 0.5F), col);
			}
			if (MirrorOn(2)) {
				float pz = edit.mirror.plane.z;
				Vector4 col = MakeVector4(0.45F, 0.6F, 1.0F, 0.25F);
				for (int i = -2; i <= stop(w); i += 2)
					renderer->AddDebugLine(MakeVector3(float(i) - 0.5F, lo, pz),
					                       MakeVector3(float(i) - 0.5F, hiY, pz), col);
				for (int i = -2; i <= stop(h); i += 2)
					renderer->AddDebugLine(MakeVector3(lo, float(i) - 0.5F, pz),
					                       MakeVector3(hiX, float(i) - 0.5F, pz), col);
			}
		}

		// --- Navigation cube -------------------------------------------------

		void KV6EditorView::FillTri(const Vector2& A, const Vector2& B, const Vector2& C,
		                            const Vector4& col) {
			// Hard-edged on purpose: triangles tiling a larger shape would show
			// seams where two anti-aliased edges meet. Use OverlayFillConvexPolygon
			// for a standalone shape.
			ColorNP(col);
			renderer->DrawFilledTriangle(A, B, C);
		}

		GizmoView KV6EditorView::GetGizmoView() const { return camera; }

		void KV6EditorView::DrawGizmo(const TransformGizmo& gizmo) {
			GizmoCanvas canvas(*renderer);
			gizmo.Draw(canvas, GetGizmoView());
		}

		// A solid, shaded cube drawn as a 2D overlay: project the camera-facing faces
		// and fill them (offered to tool scripts for solid markers).
		void KV6EditorView::DrawSolidCube(const Vector3& center, float half,
		                                  const Vector4& color) {
			Vector3 corner[8];
			for (int i = 0; i < 8; i++)
				corner[i] = center + MakeVector3((i & 1) ? half : -half, (i & 2) ? half : -half,
				                                 (i & 4) ? half : -half);
			static const int faceIdx[6][4] = {{0, 2, 6, 4}, {1, 5, 7, 3}, {0, 4, 5, 1},
			                                  {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 6, 7, 5}};
			static const float faceN[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
			                                  {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};
			for (int f = 0; f < 6; f++) {
				Vector3 n = MakeVector3(faceN[f][0], faceN[f][1], faceN[f][2]);
				Vector3 toCam = camera.eye - (center + n * half);
				float facing = Vector3::Dot(n, toCam);
				if (facing <= 0.0F)
					continue; // back-facing
				Vector2 q[4];
				bool ok = true, o;
				for (int i = 0; i < 4; i++) {
					q[i] = WorldToScreen(corner[faceIdx[f][i]], o);
					ok = ok && o;
				}
				if (!ok)
					continue;
				float sh = 0.55F + 0.45F * (facing / std::max(toCam.GetLength(), 1.0e-4F));
				Vector4 col = MakeVector4(color.x * sh, color.y * sh, color.z * sh, color.w);
				FillTri(q[0], q[1], q[2], col);
				FillTri(q[0], q[2], q[3], col);
			}
		}

		bool KV6EditorView::NaviCubeDir(const Vector2& p, Vector3& dir) {
			const std::vector<NaviFacet>& facets = NaviFacets();
			for (const NaviFacet& f : facets) {
				if (-Vector3::Dot(f.dir, camera.forward) <= 0.02F)
					continue; // back-facing
				Vector2 q[4];
				for (int i = 0; i < f.n; i++)
					q[i] = MakeVector2(gizCx + Vector3::Dot(f.v[i], camera.right) * gizR,
					                   gizCy - Vector3::Dot(f.v[i], camera.up) * gizR);
				if (PointInPoly(p, q, f.n)) { dir = f.dir; return true; }
			}
			return false;
		}

		void KV6EditorView::DrawNaviCube() {
			client::IFont& font = fontManager->GetSmallGuiFont();
			const std::vector<NaviFacet>& facets = NaviFacets();
			Vector3 hdir;
			bool hov = NaviCubeDir(softwareCursor->GetPosition(), hdir);
			// Draw bevels behind the faces: corners, then edges, then faces.
			for (int pass = 0; pass < 3; pass++) {
				for (const NaviFacet& f : facets) {
					bool isFace = f.tint >= 0;
					bool isCorner = (f.n == 3);
					bool isEdge = !isFace && !isCorner;
					if ((pass == 0 && !isCorner) || (pass == 1 && !isEdge) || (pass == 2 && !isFace))
						continue;
					float facing = -Vector3::Dot(f.dir, camera.forward);
					if (facing <= 0.02F)
						continue;
					Vector2 q[4];
					for (int i = 0; i < f.n; i++)
						q[i] = MakeVector2(gizCx + Vector3::Dot(f.v[i], camera.right) * gizR,
						                   gizCy - Vector3::Dot(f.v[i], camera.up) * gizR);
					float sh = 0.45F + 0.55F * facing;
					Vector4 base = isFace ? AxisTint(f.tint) : MakeVector4(0.5F, 0.52F, 0.56F, 1.0F);
					bool hl = hov && Vector3::Dot(hdir, f.dir) > 0.999F;
					Vector4 col = hl ? MakeVector4(0.4F, 0.7F, 1.0F, 0.97F)
					                 : MakeVector4(base.x * sh, base.y * sh, base.z * sh, 0.97F);
					// Each facet is one convex polygon so its whole outline is
					// anti-aliased; the seams it leaves against its neighbours are
					// covered by the edge lines drawn next.
					OverlayFillConvexPolygon(*renderer, q, std::size_t(f.n), col);
					Vector4 ec = MakeVector4(0.08F, 0.08F, 0.1F, 0.85F);
					for (int e = 0; e < f.n; e++)
						DrawLine2D(q[e], q[(e + 1) % f.n], 1.0F, ec);
				}
			}
			// Face labels last, so they sit on top of the cube.
			for (const NaviFacet& f : facets) {
				if (!f.label || -Vector3::Dot(f.dir, camera.forward) <= 0.02F)
					continue;
				Vector2 ctr = MakeVector2(0.0F, 0.0F);
				for (int i = 0; i < 4; i++)
					ctr += MakeVector2(gizCx + Vector3::Dot(f.v[i], camera.right) * gizR,
					                   gizCy - Vector3::Dot(f.v[i], camera.up) * gizR);
				ctr = ctr * 0.25F;
				float ls = 0.85F;
				Vector2 ts = font.Measure(f.label);
				font.DrawShadow(f.label, ctr - MakeVector2(ts.x * ls * 0.5F, ts.y * ls * 0.5F), ls,
				                MakeVector4(1, 1, 1, 1), MakeVector4(0, 0, 0, 0.8F));
			}
		}

		void KV6EditorView::SnapCameraDir(const Vector3& dir) {
			Vector3 f = dir * -1.0F; // camera forward = look toward the model
			float tp, ty;
			if (std::fabs(f.z) > 0.999F) {
				tp = asinf(std::max(-1.0F, std::min(1.0F, -f.z)));
				ty = yaw; // looking straight up/down: keep heading
			} else {
				tp = asinf(-f.z);
				ty = atan2f(f.y, f.x);
			}
			// Shortest angular path for yaw.
			while (ty - yaw > M_PI_F) ty -= 2.0F * M_PI_F;
			while (ty - yaw < -M_PI_F) ty += 2.0F * M_PI_F;
			targetYaw = ty;
			targetPitch = tp;
			camAnim = true;
		}

		std::string KV6EditorView::ToolHintLine() {
			std::string line;
			if (SamplingArmed()) {
				line = "Pick colour:  click a voxel";
			} else if (EditorTool* tool = ActiveTool()) {
				// A tool whose only sub-tool is itself goes by its own name alone.
				line = tool->Label();
				if (tool->SubToolCount() > 1)
					line += std::string(" \xE2\x80\xBA ") + tool->SubToolLabel(tool->ActiveSubTool());
				const std::string hint = tool->Hint(*this);
				if (!hint.empty())
					line += ":  " + hint;
			}
			// What Escape does next, from the same list Escape acts on.
			const std::string escape = "[Esc] " + EscapeLabel(NextEscape());
			return line.empty() ? escape : line + kHintSeparator + escape;
		}

		void KV6EditorView::DrawOverlay(float sw, float sh) {
			client::IFont& font = fontManager->GetSmallGuiFont();
			const float lineH = 22.0F;
			const float left = 16.0F;

			// Title / filename live in the ribbon; the tools' own keys are on
			// their buttons and in the tool hint, Escape in the tool hint, so this
			// line keeps to the keys that work everywhere, named as they are
			// bound: the help never names a key that does something else.
			std::string help = "[MMB] look  |  [Shift+MMB] pan";
			auto keys = [&](std::initializer_list<std::string> bound, const char* what) {
				std::string names;
				for (const std::string& key : bound) {
					if (!key.empty())
						names += (names.empty() ? "" : "/") + key;
				}
				if (!names.empty())
					help += kHintSeparator + "[" + names + "] " + what;
			};
			keys({cg_keyMoveForward, cg_keyMoveLeft, cg_keyMoveBackward, cg_keyMoveRight}, "move");
			keys({cg_keyJump, cg_keyCrouch}, "up/down");
			keys({cg_keySprint}, "faster");
			help += "  |  [Wheel] zoom  |  [Alt+LMB] pick colour  |  [Ctrl+Z/Y] undo/redo  |  "
			        "[Ctrl+C/X/V] copy/cut/paste";
			keys({cg_keyDelete}, "delete selection");

			// Text stays clear of the colour picker when it is open.
			float right = sw - left;
			ColorPicker& picker = *ui->GetColorPicker();
			if (picker.IsOpen())
				right = std::min(right, picker.GetBounds().GetMinX() - left);
			const float width = std::max(0.0F, right - left);

			// Bottom up: the help, the tool hint above it, the status above that.
			float y = sh - 28.0F;
			auto drawUp = [&](const std::string& text, const Vector4& color) {
				const std::vector<std::string> lines = WrapParts(font, text, width);
				for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
					font.Draw(*it, MakeVector2(left, y), 1.0F, color);
					y -= lineH;
				}
			};
			drawUp(help, MakeVector4(0.6F, 0.6F, 0.63F, 1.0F));
			drawUp(ToolHintLine(), MakeVector4(0.9F, 0.9F, 0.93F, 1.0F));
			if (statusTimer > 0.0F)
				drawUp(statusMessage, MakeVector4(0.5F, 1.0F, 0.6F, 1.0F));
		}

		void KV6EditorView::SetActiveTool(int index) {
			if (index >= 0 && index < int(tools.size()))
				Activate(index, currentMode);
		}

		void KV6EditorView::SetMode(EditorMode mode) { Activate(activeTool, mode); }

		void KV6EditorView::Activate(int toolIndex, EditorMode mode) {
			if (toolIndex == activeTool && mode == currentMode)
				return;
			// Leaving a tool ends what it had in progress: this is where a pending
			// placement reaches the document.
			if (EditorTool* previous = ActiveTool())
				previous->OnDeactivate(*this);
			activeTool = toolIndex;
			currentMode = mode;
			if (EditorTool* current = ActiveTool())
				current->OnActivate(*this);
		}

		const std::vector<KV6EditorView::ModeInfo>& KV6EditorView::Modes() {
			static const std::vector<ModeInfo> modes = {{EditorMode::Object, "object", "Object"},
			                                            {EditorMode::Edit, "edit", "Edit"},
			                                            {EditorMode::Animation, "animation", "Animation"}};
			return modes;
		}

		int KV6EditorView::ToolIndex(const std::string& id) const {
			for (size_t i = 0; i < tools.size(); i++) {
				if (tools[i].id == id)
					return int(i);
			}
			return -1;
		}

		ToolOption* KV6EditorView::ActiveOption(const std::string& id, ToolOption::Type type) {
			EditorTool* tool = ActiveTool();
			ToolOptions* options = tool ? tool->Options() : nullptr;
			ToolOption* option = options ? options->Find(id) : nullptr;
			return (option && option->type == type) ? option : nullptr;
		}

		std::string KV6EditorView::SubToolHotKey(int index) {
			return (index >= 0 && index < 9) ? std::to_string(index + 1) : std::string();
		}

		bool KV6EditorView::KeyIsTaken(const std::string& key) const {
			const std::string bound[] = {cg_keyMoveForward, cg_keyMoveBackward, cg_keyMoveLeft,
			                             cg_keyMoveRight,   cg_keyJump,         cg_keyCrouch,
			                             cg_keySprint,      cg_keyDelete,       cg_keyScreenshot};
			for (const std::string& b : bound) {
				if (KV6CheckKey(b, key))
					return true;
			}
			return false;
		}

		bool KV6EditorView::SwitchByHotKey(const std::string& key) {
			// Never mid-press: the tool taking over would see a drag it never saw start.
			if (key.empty() || lmbHeld || rmbHeld || KeyIsTaken(key))
				return false;
			for (size_t i = 0; i < tools.size(); i++) {
				if (!tools[i].hotKey.empty() && EqualsIgnoringCase(tools[i].hotKey, key)) {
					SetActiveTool(int(i));
					return true;
				}
			}
			EditorTool* tool = ActiveTool();
			if (!tool || tool->SubToolCount() < 2)
				return false;
			for (int sub = 0; sub < tool->SubToolCount(); sub++) {
				if (SubToolHotKey(sub) == key) {
					tool->SetSubTool(*this, sub);
					return true;
				}
			}
			return false;
		}

		EditorTool* KV6EditorView::ActiveTool() {
			if (currentMode == EditorMode::Edit && activeTool >= 0 && activeTool < int(tools.size()))
				return tools[activeTool].tool.get();
			return nullptr;
		}

		PointerInput KV6EditorView::MakePointer(PointerButton b, PointerPhase ph,
		                                        const Vector2& delta) const {
			PointerInput e;
			e.button = b;
			e.phase = ph;
			e.pos = softwareCursor->GetPosition();
			e.delta = delta;
			e.alt = altHeld;
			e.ctrl = ctrlHeld;
			e.shift = shiftHeld;
			return e;
		}

		void KV6EditorView::DispatchPointer(const PointerInput& e) {
			// A press starts one user action and the release of the last held
			// button ends it, so a stroke or a drag undoes as one step. (The held
			// flags already include this press and exclude this release.)
			if (e.IsDown() && !(lmbHeld && rmbHeld))
				undo.BeginAction();
			UserActionEnd end(*this, e.IsUp() && !lmbHeld && !rmbHeld);
			EditorTool* t = ActiveTool();
			if (!t)
				return;
			// The right button is the inverse of the left, and recolouring has
			// none: the button does nothing in Paint, whatever the sub-tool.
			if (e.IsRight() && t->Role() == EditorRole::Paint)
				return;
			t->OnPointer(*this, e);
		}

		void KV6EditorView::CancelToolInteraction() {
			if (EditorTool* t = ActiveTool())
				t->CancelInteraction(*this);
			EndUserAction(); // whatever comes next is a separate step
		}

		void KV6EditorView::EndUserAction() noexcept {
			EndPreviews();
			undo.EndAction();
		}

		void KV6EditorView::EndPreviews() noexcept {
			if (previewedOrigin) {
				ApplyOriginRaw(*previewedOrigin);
				previewedOrigin.reset();
			}
			if (previewedMirrorPlane) {
				edit.mirror.plane = *previewedMirrorPlane;
				previewedMirrorPlane.reset();
			}
		}

		void KV6EditorView::NotifyDocumentChanged() {
			if (EditorTool* t = ActiveTool())
				t->OnDocumentChanged(*this);
		}

		// --- Ribbon (title) + unified toolbar [modes] | [tools] -------------
		//
		// Two stacked full-width bars at the very top: a ribbon (title/filename)
		// above a left-aligned toolbar. The 3D viewport is drawn below them.
		void KV6EditorView::DrawToolbar(float sw, float sh) {
			screenWidth = sw; // cache for hit detection in MouseEvent
			(void)sh;
			if (!ui) return;

			// Mode buttons; the ones this document does not support are greyed out.
			std::vector<Toolbar::ToolbarButton> modeButtons;
			for (const ModeInfo& info : Modes()) {
				Toolbar::ToolbarButton btn;
				btn.id = info.id;
				btn.label = info.label;
				btn.enabled = ModeSupported(info.mode);
				btn.active = info.mode == currentMode;
				modeButtons.push_back(btn);
			}
			ui->GetToolbar()->SetModeButtons(modeButtons);

			// Tool buttons. A hot key the editor already uses for something else
			// never reaches the tool, so the button does not claim it.
			int toolCount = (currentMode == EditorMode::Edit) ? int(tools.size()) : 0;
			std::vector<Toolbar::ToolbarButton> toolButtons;
			for (int i = 0; i < toolCount; i++) {
				Toolbar::ToolbarButton btn;
				btn.id = tools[i].id;
				btn.label = tools[i].tool->Label();
				btn.hotKey = KeyIsTaken(tools[i].hotKey) ? std::string() : tools[i].hotKey;
				btn.active = (activeTool == i);
				btn.group = tools[i].group;
				toolButtons.push_back(btn);
			}
			ui->GetToolbar()->SetToolButtons(toolButtons);

			ui->GetToolbar()->SetColorSwatch(currentColor, ui->GetColorPicker()->IsOpen());

			// Set undo/redo state
			ui->GetToolbar()->SetUndoButton(undo.CanUndo());
			ui->GetToolbar()->SetRedoButton(undo.CanRedo());

			// Draw the toolbar
			ui->GetToolbar()->Draw(*renderer, *fontManager, softwareCursor->GetPosition(),
					ui->GetEditorMenu()->IsActive(), sw);
		}

		bool KV6EditorView::MirrorOn(int axis) const { return MirrorEnabled(axis); }

		bool KV6EditorView::MirrorEnabled(int axis) const {
			if (axis < 0 || axis > 2)
				return false;
			// Mirroring is a modelling aid for Edit mode; it must not silently
			// reshape anything while another mode is driving the document.
			if (currentMode != EditorMode::Edit)
				return false;
			return edit.mirror.enabled[axis];
		}

		// The mirror setup is journaled like the selection (each undo step
		// restores it), so changing it is a command like any edit.
		void KV6EditorView::SetMirrorEnabled(int axis, bool on) {
			if (axis < 0 || axis > 2)
				return;
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Mirror Axis");
			edit.mirror.enabled[axis] = on;
		}

		void KV6EditorView::SetMirrorPlane(const Vector3& plane) {
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Move Mirror");
			PlaceMirrorPlane(plane);
		}

		void KV6EditorView::PreviewMirrorPlane(const Vector3& plane) {
			if (!previewedMirrorPlane)
				previewedMirrorPlane = edit.mirror.plane;
			PlaceMirrorPlane(plane);
		}

		void KV6EditorView::ResetMirrorPlane() {
			DocumentCommand command(*this);
			KV6UndoStack::Step step(undo, "Reset Mirror");
			PlaceMirrorPlane(GetPivot());
		}

		void KV6EditorView::PlaceMirrorPlane(const Vector3& plane) {
			// MirrorIdx only sees whole half steps, so anything finer would move the
			// handle without moving the reflection.
			auto snap = [](float v) { return float(HalfSteps(v)) * 0.5F; };
			edit.mirror.plane = MakeVector3(snap(plane.x), snap(plane.y), snap(plane.z));
		}

		void KV6EditorView::DrawSubToolbar(float sw) {
			if (!ui)
				return;
			// With no tool active the bar is drawn empty, so it never keeps
			// answering clicks on buttons that are no longer there.
			EditorTool* t = ActiveTool();

			// Sub-tool buttons for the option bar. A single sub-tool is the tool
			// itself, and a lone button that is always on would only be noise.
			std::vector<OptionBar::SubToolButton> subTools;
			if (t && t->SubToolCount() > 1) {
				for (int i = 0; i < t->SubToolCount(); i++) {
					OptionBar::SubToolButton btn;
					btn.label = t->SubToolLabel(i);
					btn.hotKey = KeyIsTaken(SubToolHotKey(i)) ? std::string() : SubToolHotKey(i);
					btn.active = (t->ActiveSubTool() == i);
					subTools.push_back(btn);
				}
			}
			// Build options for the tool
			std::vector<OptionBar::Option> options;
			if (t)
				t->UpdateOptions(*this);
			ToolOptions* opts = t ? t->Options() : nullptr;
			if (opts) {
				for (int i = 0; i < opts->Count(); i++) {
					const ToolOption& op = opts->At(i);
					OptionBar::Option opt;
					opt.id = op.id;
					opt.group = op.group;
					opt.label = op.label;
					opt.type = (op.type == ToolOption::Type::Label)  ? OptionBar::OptionType::Label
							 : (op.type == ToolOption::Type::Action) ? OptionBar::OptionType::Action
							 : OptionBar::OptionType::Bool;
					opt.bvalue = op.bvalue;
					opt.enabled = op.enabled;
					options.push_back(opt);
				}
			}
			ui->GetOptionBar()->SetContent(t, std::move(subTools), std::move(options));

			// Draw the option bar
			ui->GetOptionBar()->Draw(*renderer, *fontManager, softwareCursor->GetPosition(),
					ui->GetEditorMenu()->IsActive(), sw);
		}

		void KV6EditorView::DrawRibbon(float sw) {
			client::IFont& font = fontManager->GetSmallGuiFont();
			ColorNP(MakeVector4(0.06F, 0.06F, 0.08F, 1.0F));
			FillRect(0.0F, 0.0F, sw, kRibbonH);

			std::string name = (!filePath.empty()) ? filePath : "(unsaved model)";
			if (HasUnsavedChanges())
				name += " *";
			font.Draw("KV6 Editor", MakeVector2(12.0F, 4.0F), 0.95F, MakeVector4(1, 1, 1, 1));
			font.Draw(name + "   (" + std::to_string(DocumentVoxelCount()) + " voxels)",
			          MakeVector2(120.0F, 5.0F), 0.85F, MakeVector4(0.75F, 0.75F, 0.78F, 1.0F));

			std::string cam = "[Ctrl+S] save";
			Vector2 cs = font.Measure(cam);
			font.Draw(cam, MakeVector2(sw - 12.0F - cs.x * 0.8F, 5.0F), 0.8F,
			          MakeVector4(0.6F, 0.6F, 0.63F, 1.0F));
		}

		// --- Pause menu / Save As prompt -------------------------------------

		// --- View interface ---------------------------------------------------

		void KV6EditorView::MouseEvent(float dx, float dy) {
			if (lookActive && shiftHeld) { // Shift + wheel-button drag pans
				// The cursor travels with the drag, staying on the grabbed spot
				// (it stops at the screen edge while the pan carries on).
				softwareCursor->Accumulate(dx, dy);
				PanView(dx, dy);
				return;
			}
			if (lookActive) {
				camAnim = false; // manual look cancels a navicube animation
				float sens = 0.003F;
				yaw += dx * sens;
				pitch -= dy * sens;
				float lim = M_PI_F * 0.5F - 0.01F;
				pitch = Clampf(pitch, -lim, lim);
				return;
			}
			softwareCursor->Accumulate(dx, dy);
			const Vector2& cursor = softwareCursor->GetPosition();

			// While a dialog is up the cursor drives its widgets, not the editor.
			if (ui->GetOverlay()->IsActive())
				return;

			// Forward mouse motion to color picker if it's open
			if (ui && ui->GetColorPicker()->IsOpen()) {
				ui->GetColorPicker()->MouseMove(cursor);
			}

			// Forward cursor motion over the viewport to the active tool as a Move
			// (no button) or Drag (button held) event.
			if (cursor.y < BarsH())
				return;
			PointerButton b = lmbHeld ? PointerButton::Left
			                          : (rmbHeld ? PointerButton::Right : PointerButton::None);
			PointerPhase ph = (lmbHeld || rmbHeld) ? PointerPhase::Drag : PointerPhase::Move;
			DispatchPointer(MakePointer(b, ph, MakeVector2(dx, dy)));
		}

		void KV6EditorView::WheelEvent(float x, float y) {
			if (ui->GetOverlay()->WheelEvent(x, y))
				return;
			if (ui->GetEditorMenu()->IsActive())
				return;
			orbitDist = Clampf(orbitDist * (1.0F + y * 0.1F), 2.0F, 1000.0F);
		}

		void KV6EditorView::KeyEvent(const std::string& key, bool down) {
			const Vector2& cursor = softwareCursor->GetPosition();

			// Modifier state must track the keyboard even while a modal swallows
			// keys, or a Ctrl released during the menu stays "held" afterwards.
			if (key == "Control") ctrlHeld = down;
			if (key == "Alt") altHeld = down;
			if (key == "Shift") shiftHeld = down;
			if (KV6CheckKey(cg_keySprint, key)) keySprint = down;

			// A modal dialog (file browser) owns every key while it is up.
			if (ui->GetOverlay()->KeyEvent(key, down))
				return;

			if (ui->GetEditorMenu()->KeyEvent(key, down)) {
				// Releases are swallowed too while the menu is up: drop held
				// movement/look so nothing keeps going once it closes.
				ReleaseHeldInput();
				return;
			}

			if (down && ctrlHeld && IsCtrlShortcut(key)) {
				// Ctrl is also the default descend key (cg_keyCrouch), so the view
				// has been sinking since Ctrl went down: undo that drift and stop
				// descending for the rest of this press.
				if (KV6CheckKey(cg_keyCrouch, "Control")) {
					orbitTarget -= ctrlDescent;
					ctrlDescent = MakeVector3(0, 0, 0);
					keyDown = false;
				}
				// A shortcut changes the document or the placement under a drag
				// in progress (a paste replaces it), so that drag is abandoned.
				CancelToolInteraction();
				if (EqualsIgnoringCase(key, "s")) Save();
				else if (EqualsIgnoringCase(key, "c")) CopySelection();
				else if (EqualsIgnoringCase(key, "x")) CutSelection();
				else if (EqualsIgnoringCase(key, "v")) Paste();
				else if (EqualsIgnoringCase(key, "z")) { if (shiftHeld) Redo(); else Undo(); }
				else Redo(); // "y"
				return;
			}

			if (key == "MiddleMouseButton") { lookActive = down; return; }

			// A release reaches the tool only when its press did: presses the
			// picker, the bars, the navigation cube or colour sampling took are
			// none of the tool's business, and start no user action to end.
			if (key == "LeftMouseButton") {
				if (!down) {
					ui->GetColorPicker()->MouseUp();
					if (lmbHeld) {
						lmbHeld = false;
						DispatchPointer(MakePointer(PointerButton::Left, PointerPhase::Up));
					}
					return;
				}

				// The open picker owns every press on its panel, gaps included, so
				// nothing behind it is ever edited through it.
				if (ui->GetColorPicker()->IsOverPicker(cursor)) {
					ui->GetColorPicker()->MouseDown(cursor);
					return;
				}

				// Over the bars, a click belongs to whichever button is under it
				// (the callbacks wired in the constructor act on it), or to nothing.
				if (cursor.y < BarsH()) {
					if (!ui->GetToolbar()->Click(cursor, screenWidth))
						ui->GetOptionBar()->Click(cursor);
					return;
				}

				// Check navigation cube
				Vector3 navDir;
				if (NaviCubeDir(cursor, navDir)) { SnapCameraDir(navDir); return; }

				// Sampling a colour works the same in every tool, which never sees
				// the click.
				if (SamplingArmed()) {
					SampleColor();
					return;
				}

				// Otherwise forward to active tool
				lmbHeld = true;
				DispatchPointer(MakePointer(PointerButton::Left, PointerPhase::Down));
				return;
			}
			if (key == "RightMouseButton") {
				if (!down) {
					if (rmbHeld) {
						rmbHeld = false;
						DispatchPointer(MakePointer(PointerButton::Right, PointerPhase::Up));
					}
					return;
				}
				// Don't allow RMB actions over the UI bars or color picker
				if (ui && ui->GetColorPicker()->IsOverPicker(cursor)) return;
				if (cursor.y < BarsH()) return; // over the bars
				rmbHeld = true;
				DispatchPointer(MakePointer(PointerButton::Right, PointerPhase::Down));
				return;
			}

			if (down && KV6CheckKey(cg_keyScreenshot, key)) { wantScreenShot = true; return; }
			if (down && KV6CheckKey(cg_keyDelete, key)) {
				// Like a Ctrl shortcut, it edits behind the tool's back.
				CancelToolInteraction();
				DeleteSelection();
				return;
			}

			std::string fwd = cg_keyMoveForward, bk = cg_keyMoveBackward, lf = cg_keyMoveLeft;
			std::string rt = cg_keyMoveRight, jp = cg_keyJump, cr = cg_keyCrouch;
			if (KV6CheckKey(fwd, key)) { keyFwd = down; return; }
			if (KV6CheckKey(bk, key)) { keyBack = down; return; }
			if (KV6CheckKey(lf, key)) { keyLeft = down; return; }
			if (KV6CheckKey(rt, key)) { keyRight = down; return; }
			if (KV6CheckKey(jp, key)) { keyUp = down; return; }
			if (KV6CheckKey(cr, key)) {
				keyDown = down;
				if (down)
					descendPressTime = globalTime; // starts the grace period
				ctrlDescent = MakeVector3(0, 0, 0); // a new press, or a finished one
				return;
			}

			// Tool and sub-tool keys, shown on their buttons. Plain keys only, so
			// a chord never switches tools.
			if (down && !ctrlHeld && !altHeld && SwitchByHotKey(key))
				return;

			// Remaining keys go to the active tool (e.g. Select's [L]).
			if (EditorTool* t = ActiveTool()) {
				KeyInput e;
				e.key = key;
				e.phase = down ? KeyPhase::Down : KeyPhase::Up;
				e.alt = altHeld;
				e.ctrl = ctrlHeld;
				e.shift = shiftHeld;
				// A key press is one user action of its own, unless it comes during
				// a press of a mouse button, whose action it then joins.
				const bool ownAction = !lmbHeld && !rmbHeld;
				if (ownAction)
					undo.BeginAction();
				UserActionEnd end(*this, ownAction);
				t->OnKey(*this, e);
			}
		}

		void KV6EditorView::TextInputEvent(const std::string& text) {
			if (ui->GetOverlay()->IsActive()) {
				ui->GetOverlay()->TextInputEvent(text);
				return;
			}
			ui->GetEditorMenu()->TextInputEvent(text);
		}

		void KV6EditorView::TextEditingEvent(const std::string& text, int start, int len) {
			ui->GetOverlay()->TextEditingEvent(text, start, len);
		}

		bool KV6EditorView::AcceptsTextInput() {
			return ui->GetOverlay()->AcceptsTextInput() || ui->GetEditorMenu()->AcceptsTextInput();
		}

		AABB2 KV6EditorView::GetTextInputRect() {
			if (ui->GetOverlay()->IsActive())
				return ui->GetOverlay()->GetTextInputRect();
			return ui->GetEditorMenu()->GetTextInputRect();
		}

		void KV6EditorView::RunFrame(float dt) {
			SPADES_MARK_FUNCTION();
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();
			globalTime += dt;

			// Smoothly rotate toward a navicube-selected view.
			if (camAnim) {
				float k = std::min(1.0F, dt * 12.0F);
				yaw += (targetYaw - yaw) * k;
				pitch += (targetPitch - pitch) * k;
				if (std::fabs(targetYaw - yaw) < 0.002F && std::fabs(targetPitch - pitch) < 0.002F) {
					yaw = targetYaw;
					pitch = targetPitch;
					camAnim = false;
				}
			}

			if (!ui->GetEditorMenu()->IsActive() && !ui->GetOverlay()->IsActive())
				UpdateMovement(dt);
			if (statusTimer > 0.0F)
				statusTimer -= dt;

			renderer->SetFogColor(MakeVector3(0.10F, 0.10F, 0.12F));
			// Matches the far plane: fog that ended closer would swallow the model
			// at the far end of the zoom range.
			renderer->SetFogDistance(ViewDistance());

			// The scene is rendered full-screen (sub-viewport rendering isn't
			// guaranteed across renderers — keep this renderer-agnostic), and the
			// ribbon + toolbar bars are drawn opaque over the top. So the camera
			// projection and the cursor->ray pick both use the full screen.
			client::SceneDefinition sceneDef = SetupScene(0.0F, 0.0F, sw, sh);
			camera.eye = sceneDef.viewOrigin;
			camera.right = sceneDef.viewAxis[0];
			camera.up = sceneDef.viewAxis[1];
			camera.forward = sceneDef.viewAxis[2];
			camera.tanHalfFovX = tanf(sceneDef.fovX * 0.5F);
			camera.tanHalfFovY = tanf(sceneDef.fovY * 0.5F);
			camera.viewportX = 0.0F;
			camera.viewportY = 0.0F;
			camera.viewportWidth = sw;
			camera.viewportHeight = sh;

			RefreshRenderModels(); // what edits since the last frame left stale
			renderer->StartScene(sceneDef);
			if (renderModel) {
				client::ModelRenderParam param;
				// Cancel the KV6 pivot so the model sits in the editor's grid space.
				param.matrix = Matrix4::Translate(model->GetOrigin() * -1.0F);
				renderer->RenderModel(*renderModel, param);
			}
			// Pending voxels (a paste, an import, or a selection being moved) render
			// solid at their temporary position, in a document that no longer holds
			// them.
			if (edit.placing && placementModel) {
				client::ModelRenderParam param;
				param.matrix = Matrix4::Translate(MakeVector3(float(edit.placement.anchor.x),
				                                             float(edit.placement.anchor.y),
				                                             float(edit.placement.anchor.z)));
				renderer->RenderModel(*placementModel, param);
			}
			DrawHelpers();
			DrawOriginAxes();
			DrawMirrorPlanes();
			DrawSelection();

			EditorTool* tool = ActiveTool();
			if (tool)
				tool->DrawScene(*this);
			// Voxels waiting to be placed, drawn in their own colours.
			if (edit.placing)
				DrawPlacementPreview();
			renderer->EndScene();

			DrawOverlayLines2D(); // outlines and tool wires, over the finished scene
			// The picker is laid out first: the text under the viewport keeps clear of it.
			ui->GetColorPicker()->UpdateLayout(sw, sh, BarsH());
			DrawOverlay(sw, sh);
			DrawRibbon(sw);
			DrawToolbar(sw, sh);
			DrawSubToolbar(sw);

			// Layout and draw navigation cube
			float gizBox = 174.0F;
			gizR = gizBox * 0.5F - 14.0F;
			gizCx = sw - 16.0F - gizBox * 0.5F;
			gizCy = BarsH() + 8.0F + gizBox * 0.5F;
			DrawNaviCube();

			// Draw color picker
			if (ui) {
				ui->GetColorPicker()->Draw(*renderer, *fontManager, softwareCursor->GetPosition());
			}

			if (tool)
				tool->DrawOverlay(*this);

			ui->GetEditorMenu()->Draw();

			// Dialogs draw their own pointer (with the text-field I-beam), so the
			// editor's cursor steps aside while one is open.
			ui->GetOverlay()->RunFrame(dt);
			ui->GetOverlay()->Draw();
			if (!ui->GetOverlay()->IsActive())
				softwareCursor->Draw();

			// The runner is the only thing that presents, so the capture happens here
			// rather than after the frame: `FrameDone` flushes everything drawn above so
			// the read-back is a complete image. Same shape as `Client::TakeScreenShot`.
			// Shares the client's Screenshots/ numbering and cg_screenshotFormat.
			if (wantScreenShot) {
				wantScreenShot = false;
				renderer->FrameDone();
				try {
					std::string name = client::SaveScreenShot(*renderer);
					SetStatus("Screenshot saved: " + name);
				} catch (const std::exception& ex) {
					SetStatus("Screenshot failed");
					SPLog("Editor screenshot failed: %s", ex.what());
				}
			}
		}
	} // namespace gui
} // namespace spades
