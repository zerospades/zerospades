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

#include "KV6EditorView.h"
#include "KV6EditorTool.h"
#include "KV6ScreenHelper.h"
#include "KV6ToolRegistry.h"
#include "UIWidgetPainter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IImage.h>
#include <Client/IModel.h>
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Core/VoxelModel.h>

SPADES_SETTING(cg_keyMoveForward);
SPADES_SETTING(cg_keyMoveBackward);
SPADES_SETTING(cg_keyMoveLeft);
SPADES_SETTING(cg_keyMoveRight);
SPADES_SETTING(cg_keyJump);
SPADES_SETTING(cg_keyCrouch);

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

			float Clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

			// Top UI bands (full width): a title ribbon above the toolbar. The 3D
			// viewport is inset below them by kBarsH.
			const float kRibbonH = 24.0F;
			const float kToolbarH = 32.0F;
			const float kSubBarH = 28.0F; // secondary toolbar (active tool's sub-tools)
			const float kBarsH = kRibbonH + kToolbarH + kSubBarH; // always-present bars
			const float kTbBtn = 84.0F, kTbH = 24.0F, kTbGap = 2.0F, kTbSep = 14.0F;
			const float kSubBtn = 88.0F;  // sub-tool button width
			const float kUndoBtnW = 50.0F; // Undo / Redo button width (toolbar right edge)
			const float kTbY = kRibbonH + (kToolbarH - kTbH) * 0.5F;
			const float kTbX0 = 12.0F; // toolbar sticks to the left edge
			const float kMirW = 24.0F, kMirLabelW = 50.0F; // option toggle / group label
			const float kColorW = 46.0F;                   // current-colour swatch
			const float kLabelW = 190.0F;                  // read-only readout (e.g. pivot)

			// Pack a (non-negative) voxel coordinate into a selection-set key.
			int64_t SelKey(int x, int y, int z) {
				return (int64_t(x & 0xFFFFF) << 40) | (int64_t(y & 0xFFFFF) << 20) |
				       int64_t(z & 0xFFFFF);
			}
			void SelDecode(int64_t k, int& x, int& y, int& z) {
				x = int((k >> 40) & 0xFFFFF);
				y = int((k >> 20) & 0xFFFFF);
				z = int(k & 0xFFFFF);
			}

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
		                             client::FontManager* fm, const std::string& path, bool isNew)
		    : renderer(r), audioDevice(dev), fontManager(fm) {
			SPADES_MARK_FUNCTION();
			io = Handle<KV6ScreenHelper>::New();
			cursorImg = renderer->RegisterImage("Gfx/UI/Cursor.png");
			cursor = MakeVector2(renderer->ScreenWidth() * 0.5F, renderer->ScreenHeight() * 0.5F);

			BuildPresets();
			RGBToHSV(currentColor);
			LayoutPicker();

			// The Edit-mode tools come from the registry (toolbar order = registration).
			// Pivot is leftmost, but open on Draw so the user can start modelling.
			ToolRegistry::Instance().BuildAll(tools);
			activeTool = 0;
			for (size_t i = 0; i < tools.size(); i++) {
				if (std::string(tools[i]->Label()) == "Draw") {
					activeTool = int(i);
					break;
				}
			}

			if (isNew || path.empty())
				NewModel(cubeSize, path);
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
			freePos = CameraEye();
		}

		void KV6EditorView::NewModel(int n, const std::string& path) {
			cubeSize = n;
			model = Handle<VoxelModel>::New(n, n, n);
			model->SetSolid(n / 2, n / 2, n / 2, currentColor);
			// Anchor the pivot on the seed voxel so mirroring (which reflects across
			// the pivot) is symmetric about it from the start.
			float c = float(n / 2);
			model->SetOrigin(MakeVector3(-c, -c, -c));
			voxelCount = 1;
			RebuildRenderModel();
			filePath = path;
			FrameCamera();
			undo.Clear();
			savedGeomId = -1; // a fresh, never-saved document starts dirty
		}

		void KV6EditorView::LoadModel(const std::string& path) {
			VoxelModel* loaded = io->Load(path);
			if (!loaded) {
				NewModel(cubeSize, path);
				SetStatus("Could not load " + path);
				return;
			}
			model = Handle<VoxelModel>(loaded, false); // adopt (Load returns a ref)
			cubeSize = std::max(model->GetWidth(), std::max(model->GetHeight(), model->GetDepth()));
			voxelCount = CountSolids();
			RebuildRenderModel();
			filePath = path;
			FrameCamera();
			undo.Clear();
			savedGeomId = undo.GeometryStateId(); // a freshly loaded document is clean
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

		void KV6EditorView::RebuildRenderModel() { renderModel = renderer->CreateModel(*model); }

		void KV6EditorView::SetStatus(const std::string& s) {
			statusMessage = s;
			statusTimer = 2.5F;
		}

		void KV6EditorView::Save() {
			if (filePath.empty()) {
				SetStatus("No file to save to");
				return;
			}
			if (io->Save(&*model, filePath)) {
				savedGeomId = undo.GeometryStateId(); // this geometry state is now clean
				SetStatus("Saved " + filePath);
			} else {
				SetStatus("Save failed");
			}
		}

		// --- Camera -----------------------------------------------------------

		Vector3 KV6EditorView::Forward() const {
			float cp = cosf(pitch);
			return MakeVector3(cp * cosf(yaw), cp * sinf(yaw), -sinf(pitch));
		}

		Vector3 KV6EditorView::CameraEye() const {
			if (orbitMode)
				return orbitTarget - Forward() * orbitDist;
			return freePos;
		}

		void KV6EditorView::ToggleCameraMode() {
			Vector3 eye = CameraEye();
			if (orbitMode) {
				freePos = eye;
				orbitMode = false;
			} else {
				orbitTarget = eye + Forward() * orbitDist;
				orbitMode = true;
			}
		}

		void KV6EditorView::UpdateMovement(float dt) {
			if (orbitMode)
				return;
			Vector3 fwd = Forward();
			Vector3 up = MakeVector3(0.0F, 0.0F, -1.0F);
			Vector3 right = Vector3::Cross(fwd, up).Normalize();

			Vector3 move = MakeVector3(0, 0, 0);
			if (keyFwd) move += fwd;
			if (keyBack) move -= fwd;
			if (keyRight) move += right;
			if (keyLeft) move -= right;
			if (keyUp) move += up;
			if (keyDown) move -= up;

			if (move.x != 0.0F || move.y != 0.0F || move.z != 0.0F)
				freePos += move.Normalize() * (float(cubeSize) * 0.7F * dt);
		}

		client::SceneDefinition KV6EditorView::SetupScene(float vpX, float vpY, float vpW, float vpH) {
			client::SceneDefinition sceneDef;
			Vector3 eye = CameraEye();
			Vector3 at = orbitMode ? orbitTarget : (eye + Forward());
			Vector3 up = MakeVector3(0.0F, 0.0F, -1.0F);

			Vector3 dir = (at - eye).Normalize();
			Vector3 side = Vector3::Cross(dir, up).Normalize();
			up = -Vector3::Cross(dir, side);

			sceneDef.viewOrigin = eye;
			sceneDef.viewAxis[0] = side;
			sceneDef.viewAxis[1] = up;
			sceneDef.viewAxis[2] = dir;
			sceneDef.fovY = 60.0F * M_PI_F / 180.0F;
			sceneDef.fovX = 2.0F * atanf(tanf(sceneDef.fovY * 0.5F) * (vpW / vpH));
			sceneDef.zNear = 0.1F;
			sceneDef.zFar = 1000.0F;
			sceneDef.viewportLeft = int(vpX);
			sceneDef.viewportTop = int(vpY);
			sceneDef.viewportWidth = int(vpW);
			sceneDef.viewportHeight = int(vpH);
			sceneDef.skipWorld = true;
			sceneDef.denyCameraBlur = true;
			sceneDef.time = (unsigned int)(globalTime * 1000.0F);
			return sceneDef;
		}

		// --- Editing ----------------------------------------------------------

		bool KV6EditorView::RayPlaneCell(const Vector3& planePoint, const Vector3& normal,
		                                 IntVector3& out) {
			if (camSW <= 0.0F || camSH <= 0.0F)
				return false;
			float sx = ((cursor.x - camVpX) / camSW) * 2.0F - 1.0F;
			float sy = ((cursor.y - camVpY) / camSH) * 2.0F - 1.0F;
			Vector3 dir = camFwd + camRight * (sx * tanf(camFovX * 0.5F)) -
			              camUp * (sy * tanf(camFovY * 0.5F));
			dir = dir.Normalize();
			float denom = Vector3::Dot(dir, normal);
			if (std::fabs(denom) < 1.0e-5F)
				return false;
			float t = Vector3::Dot(planePoint - camEye, normal) / denom;
			if (t <= 0.0F)
				return false;
			Vector3 hit = camEye + dir * t;
			out = MakeIntVector3(int(std::floor(hit.x + 0.5F)), int(std::floor(hit.y + 0.5F)),
			                     int(std::floor(hit.z + 0.5F)));
			return true;
		}

		Vector2 KV6EditorView::WorldToScreen(const Vector3& w, bool& ok) const {
			Vector3 rel = w - camEye;
			float fz = Vector3::Dot(rel, camFwd);
			ok = fz > 0.001F;
			if (!ok)
				fz = 0.001F;
			float fx = Vector3::Dot(rel, camRight);
			float fy = Vector3::Dot(rel, camUp);
			float sx = (fx / fz) / tanf(camFovX * 0.5F);
			float sy = -(fy / fz) / tanf(camFovY * 0.5F);
			return MakeVector2(camVpX + (sx + 1.0F) * 0.5F * camSW,
			                   camVpY + (sy + 1.0F) * 0.5F * camSH);
		}

		// A bright depth-tested 3D line, also collected for the dim see-through pass.
		void KV6EditorView::EmitLine(const Vector3& a, const Vector3& b, const Vector4& color) {
			renderer->AddDebugLine(a, b, color);
			overlayLines.push_back({a, b, color});
		}

		void KV6EditorView::DrawLine3D(const Vector3& a, const Vector3& b, const Vector4& color) {
			EmitLine(a, b, color);
		}

		// Re-draw the collected overlay lines as dim 2D lines (always on top), so the
		// parts hidden behind voxels still show. Cleared each frame.
		void KV6EditorView::DrawOverlayLines2D() {
			for (const OverlayLine& l : overlayLines) {
				bool ok1, ok2;
				Vector2 pa = WorldToScreen(l.a, ok1);
				Vector2 pb = WorldToScreen(l.b, ok2);
				if (!ok1 || !ok2)
					continue; // endpoint behind the camera
				Vector4 c = l.color;
				c.w *= 0.35F; // dimmer than the visible (depth-tested) line
				DrawLine2D(pa, pb, 1.0F, c);
			}
			overlayLines.clear();
		}

		void KV6EditorView::DoPick() {
			pickHit = false;
			if (camSW <= 0.0F || camSH <= 0.0F)
				return;

			float sx = ((cursor.x - camVpX) / camSW) * 2.0F - 1.0F;
			float sy = ((cursor.y - camVpY) / camSH) * 2.0F - 1.0F;
			Vector3 dir = camFwd + camRight * (sx * tanf(camFovX * 0.5F)) -
			              camUp * (sy * tanf(camFovY * 0.5F));
			dir = dir.Normalize();

			// The renderer centres voxel (x,y,z) at world (x,y,z), so traverse in a
			// +0.5-shifted space where each integer cell maps to a voxel index.
			Vector3 o = camEye + MakeVector3(0.5F, 0.5F, 0.5F);

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

		int KV6EditorView::MirrorIdx(int i, float pivot) const {
			return int(std::floor(2.0F * pivot + 0.5F)) - i;
		}

		void KV6EditorView::ExpandMirrors(std::vector<IntVector3>& cells) const {
			bool mx = MirrorOn(0), my = MirrorOn(1), mz = MirrorOn(2);
			if (!mx && !my && !mz)
				return;
			Vector3 org = model->GetOrigin();
			std::vector<IntVector3> out;
			out.reserve(cells.size() * 8);
			for (const IntVector3& c : cells) {
				int xs[2] = {c.x, c.x}, nx = 1;
				int ys[2] = {c.y, c.y}, ny = 1;
				int zs[2] = {c.z, c.z}, nz = 1;
				if (mx) { int m = MirrorIdx(c.x, -org.x); if (m != c.x) { xs[1] = m; nx = 2; } }
				if (my) { int m = MirrorIdx(c.y, -org.y); if (m != c.y) { ys[1] = m; ny = 2; } }
				if (mz) { int m = MirrorIdx(c.z, -org.z); if (m != c.z) { zs[1] = m; nz = 2; } }
				for (int a = 0; a < nx; a++)
				for (int b = 0; b < ny; b++)
				for (int d = 0; d < nz; d++)
					out.push_back(MakeIntVector3(xs[a], ys[b], zs[d]));
			}
			cells.swap(out);
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
			orbitTarget += shift;
			freePos += shift;
			cubeSize = std::max(nw, std::max(nh, nd));
			ShiftSelection(ox, oy, oz); // keep selected voxel coords aligned
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
			DoPick();
			if (!pickHit)
				return;
			int tx = pickPX, ty = pickPY, tz = pickPZ;

			// Target plus its mirror images across the pivot plane. The mirror — or
			// a placement past an edge — may land outside, so grow to fit them all.
			Vector3 org = model->GetOrigin();
			bool mx = MirrorOn(0), my = MirrorOn(1), mz = MirrorOn(2);
			int xb = mx ? MirrorIdx(tx, -org.x) : tx; int nx = (mx && xb != tx) ? 2 : 1;
			int yb = my ? MirrorIdx(ty, -org.y) : ty; int ny = (my && yb != ty) ? 2 : 1;
			int zb = mz ? MirrorIdx(tz, -org.z) : tz; int nz = (mz && zb != tz) ? 2 : 1;

			int loX = std::min(0, tx); int hiX = std::max(model->GetWidth(), tx + 1);
			if (nx == 2) { loX = std::min(loX, xb); hiX = std::max(hiX, xb + 1); }
			int loY = std::min(0, ty); int hiY = std::max(model->GetHeight(), ty + 1);
			if (ny == 2) { loY = std::min(loY, yb); hiY = std::max(hiY, yb + 1); }
			int loZ = std::min(0, tz); int hiZ = std::max(model->GetDepth(), tz + 1);
			if (nz == 2) { loZ = std::min(loZ, zb); hiZ = std::max(hiZ, zb + 1); }

			int nw = hiX - loX, nh = hiY - loY, nd = hiZ - loZ;
			if (nw > 4096 || nh > 4096 || nd > 64) {
				SetStatus("Reached the maximum model size");
				return;
			}
			int ox = -loX, oy = -loY, oz = -loZ;
			undo.Begin("Place");
			if (ox != 0 || oy != 0 || oz != 0 || nw != model->GetWidth() ||
			    nh != model->GetHeight() || nd != model->GetDepth())
				RebuildVolume(nw, nh, nd, ox, oy, oz);
			tx += ox; xb += ox; ty += oy; yb += oy; tz += oz; zb += oz;

			bool any = false;
			for (int ia = 0; ia < nx; ia++) { int X = (ia == 0) ? tx : xb;
			for (int ib = 0; ib < ny; ib++) { int Y = (ib == 0) ? ty : yb;
			for (int ic = 0; ic < nz; ic++) { int Z = (ic == 0) ? tz : zb;
				if (!model->IsSolid(X, Y, Z)) {
					WriteVoxel(X, Y, Z, true, currentColor);
					any = true;
				}
			}}}
			undo.End();
			if (any)
				RebuildRenderModel();
		}

		void KV6EditorView::DeleteCube() {
			DoPick();
			if (!pickHit)
				return;
			int hx = pickHX, hy = pickHY, hz = pickHZ;
			Vector3 org = model->GetOrigin();
			bool mx = MirrorOn(0), my = MirrorOn(1), mz = MirrorOn(2);
			int xb = mx ? MirrorIdx(hx, -org.x) : hx; int nx = (mx && xb != hx) ? 2 : 1;
			int yb = my ? MirrorIdx(hy, -org.y) : hy; int ny = (my && yb != hy) ? 2 : 1;
			int zb = mz ? MirrorIdx(hz, -org.z) : hz; int nz = (mz && zb != hz) ? 2 : 1;

			int n = 0;
			for (int ia = 0; ia < nx; ia++) { int X = (ia == 0) ? hx : xb;
			for (int ib = 0; ib < ny; ib++) { int Y = (ib == 0) ? hy : yb;
			for (int ic = 0; ic < nz; ic++) { int Z = (ic == 0) ? hz : zb;
				if (InBounds(X, Y, Z) && model->IsSolid(X, Y, Z))
					n++;
			}}}
			if (n == 0 || voxelCount - n < 1)
				return;
			undo.Begin("Delete");
			for (int ia = 0; ia < nx; ia++) { int X = (ia == 0) ? hx : xb;
			for (int ib = 0; ib < ny; ib++) { int Y = (ib == 0) ? hy : yb;
			for (int ic = 0; ic < nz; ic++) { int Z = (ic == 0) ? hz : zb;
				if (InBounds(X, Y, Z) && model->IsSolid(X, Y, Z)) {
					WriteVoxel(X, Y, Z, false, 0);
					selection.erase(SelKey(X, Y, Z)); // drop stale selection entries
				}
			}}}
			TrimVolume();
			undo.End();
			RebuildRenderModel();
		}

		// --- Colour -----------------------------------------------------------

		uint32_t KV6EditorView::PackRGB(float r, float g, float b) const {
			int ri = std::max(0, std::min(255, int(r * 255.0F + 0.5F)));
			int gi = std::max(0, std::min(255, int(g * 255.0F + 0.5F)));
			int bi = std::max(0, std::min(255, int(b * 255.0F + 0.5F)));
			return uint32_t((bi << 16) | (gi << 8) | ri); // 0x00BBGGRR
		}

		uint32_t KV6EditorView::HSV(float h, float s, float v) const {
			float i = std::floor(h * 6.0F);
			float f = h * 6.0F - i;
			float p = v * (1.0F - s);
			float q = v * (1.0F - f * s);
			float t = v * (1.0F - (1.0F - f) * s);
			int ii = int(i) % 6;
			if (ii < 0) ii += 6;
			if (ii == 0) return PackRGB(v, t, p);
			if (ii == 1) return PackRGB(q, v, p);
			if (ii == 2) return PackRGB(p, v, t);
			if (ii == 3) return PackRGB(p, q, v);
			if (ii == 4) return PackRGB(t, p, v);
			return PackRGB(v, p, q);
		}

		Vector4 KV6EditorView::ColorToVec(uint32_t c) const {
			return MakeVector4(float(c & 0xFF) / 255.0F, float((c >> 8) & 0xFF) / 255.0F,
			                   float((c >> 16) & 0xFF) / 255.0F, 1.0F);
		}

		void KV6EditorView::BuildPresets() {
			presets.clear();
			for (int c = 0; c < presetCols; c++) {
				float g = float(c) / float(presetCols - 1);
				presets.push_back(PackRGB(g, g, g));
			}
			for (int c = 0; c < presetCols; c++)
				presets.push_back(HSV(float(c) / float(presetCols), 1.0F, 1.0F));
		}

		void KV6EditorView::RGBToHSV(uint32_t c) {
			float r = float(c & 0xFF) / 255.0F;
			float g = float((c >> 8) & 0xFF) / 255.0F;
			float b = float((c >> 16) & 0xFF) / 255.0F;
			float mx = std::max(r, std::max(g, b));
			float mn = std::min(r, std::min(g, b));
			float d = mx - mn;
			val = mx;
			sat = (mx <= 0.0F) ? 0.0F : d / mx;
			float h = 0.0F;
			if (d > 0.0F) {
				if (mx == r) h = (g - b) / d + (g < b ? 6.0F : 0.0F);
				else if (mx == g) h = (b - r) / d + 2.0F;
				else h = (r - g) / d + 4.0F;
				h /= 6.0F;
			}
			hue = h;
		}

		void KV6EditorView::SyncColor() { currentColor = HSV(hue, sat, val); }

		void KV6EditorView::Eyedropper() {
			DoPick();
			if (!pickHit)
				return;
			currentColor = model->GetColor(pickHX, pickHY, pickHZ) & 0xFFFFFF;
			RGBToHSV(currentColor);
			SetStatus("Picked colour");
		}

		// --- Selection --------------------------------------------------------

		void KV6EditorView::ToggleSelect(int x, int y, int z) {
			undo.Begin("Select");
			int64_t k = SelKey(x, y, z);
			auto it = selection.find(k);
			if (it == selection.end())
				selection.insert(k);
			else
				selection.erase(it);
			undo.End();
		}
		void KV6EditorView::AddSelect(int x, int y, int z) { selection.insert(SelKey(x, y, z)); }
		bool KV6EditorView::IsSelected(int x, int y, int z) const {
			return selection.find(SelKey(x, y, z)) != selection.end();
		}
		void KV6EditorView::ClearSelection() {
			undo.Begin("Clear Selection");
			selection.clear();
			undo.End();
		}

		void KV6EditorView::SelectLinkedColor(int x, int y, int z) {
			if (!InBounds(x, y, z) || !model->IsSolid(x, y, z))
				return;
			undo.Begin("Select Colour");
			uint32_t target = model->GetColor(x, y, z) & 0xFFFFFF;
			std::set<int64_t> visited;
			std::vector<IntVector3> stack;
			stack.push_back(MakeIntVector3(x, y, z));
			int added = 0;
			while (!stack.empty()) {
				IntVector3 c = stack.back();
				stack.pop_back();
				int64_t key = SelKey(c.x, c.y, c.z);
				if (!visited.insert(key).second)
					continue;
				if (!InBounds(c.x, c.y, c.z) || !model->IsSolid(c.x, c.y, c.z))
					continue;
				if ((model->GetColor(c.x, c.y, c.z) & 0xFFFFFF) != target)
					continue;
				AddSelect(c.x, c.y, c.z);
				added++;
				stack.push_back(MakeIntVector3(c.x + 1, c.y, c.z));
				stack.push_back(MakeIntVector3(c.x - 1, c.y, c.z));
				stack.push_back(MakeIntVector3(c.x, c.y + 1, c.z));
				stack.push_back(MakeIntVector3(c.x, c.y - 1, c.z));
				stack.push_back(MakeIntVector3(c.x, c.y, c.z + 1));
				stack.push_back(MakeIntVector3(c.x, c.y, c.z - 1));
			}
			undo.End();
			SetStatus("Selected " + std::to_string(added) + " linked voxels");
		}

		void KV6EditorView::DrawSelection() {
			Vector4 col = MakeVector4(1.0F, 0.55F, 0.1F, 0.95F);
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				DrawCellOutline(x, y, z, col);
			}
		}
		void KV6EditorView::ShiftSelection(int ox, int oy, int oz) {
			if (ox == 0 && oy == 0 && oz == 0)
				return;
			std::set<int64_t> shifted;
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				shifted.insert(SelKey(x + ox, y + oy, z + oz));
			}
			selection.swap(shifted);
		}

		// --- Clipboard / paste -----------------------------------------------

		void KV6EditorView::CopySelection() {
			if (selection.empty()) {
				SetStatus("Nothing selected");
				return;
			}
			int minX = model->GetWidth(), minY = model->GetHeight(), minZ = model->GetDepth();
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				minX = std::min(minX, x); minY = std::min(minY, y); minZ = std::min(minZ, z);
			}
			clipboard.clear();
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				if (InBounds(x, y, z) && model->IsSolid(x, y, z))
					clipboard.push_back({MakeIntVector3(x - minX, y - minY, z - minZ),
					                     model->GetColor(x, y, z) & 0xFFFFFF});
			}
			SetStatus("Copied " + std::to_string(clipboard.size()) + " voxels");
		}

		bool KV6EditorView::CutSelection() {
			if (selection.empty()) {
				SetStatus("Nothing selected");
				return false;
			}
			int cut = 0;
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				if (InBounds(x, y, z) && model->IsSolid(x, y, z))
					cut++;
			}
			if (voxelCount - cut < 1) {
				SetStatus("Cannot cut every voxel");
				return false;
			}
			CopySelection();
			undo.Begin("Cut");
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				if (InBounds(x, y, z) && model->IsSolid(x, y, z))
					WriteVoxel(x, y, z, false, 0);
			}
			selection.clear();
			TrimVolume();
			undo.End();
			RebuildRenderModel();
			SetStatus("Cut " + std::to_string(clipboard.size()) + " voxels");
			return true;
		}

		void KV6EditorView::StartPaste() {
			if (clipboard.empty()) {
				SetStatus("Clipboard is empty");
				return;
			}
			pasteActive = true;
			pasteAnchor = MakeIntVector3(model->GetWidth() / 2, model->GetHeight() / 2,
			                             model->GetDepth() / 2);
			SetStatus("Paste: click to place, [Esc] to cancel");
		}

		void KV6EditorView::PasteClipboard(const IntVector3& anchor) {
			if (clipboard.empty())
				return;
			int loX = 0, loY = 0, loZ = 0;
			int hiX = model->GetWidth(), hiY = model->GetHeight(), hiZ = model->GetDepth();
			for (const ClipVoxel& v : clipboard) {
				int x = anchor.x + v.rel.x, y = anchor.y + v.rel.y, z = anchor.z + v.rel.z;
				loX = std::min(loX, x); hiX = std::max(hiX, x + 1);
				loY = std::min(loY, y); hiY = std::max(hiY, y + 1);
				loZ = std::min(loZ, z); hiZ = std::max(hiZ, z + 1);
			}
			int nw = hiX - loX, nh = hiY - loY, nd = hiZ - loZ;
			if (nw > 4096 || nh > 4096 || nd > 64) {
				SetStatus("Reached the maximum model size");
				return;
			}
			int ox = -loX, oy = -loY, oz = -loZ;
			undo.Begin("Paste");
			if (ox != 0 || oy != 0 || oz != 0 || nw != model->GetWidth() ||
			    nh != model->GetHeight() || nd != model->GetDepth())
				RebuildVolume(nw, nh, nd, ox, oy, oz);
			selection.clear();
			for (const ClipVoxel& v : clipboard) {
				int x = anchor.x + v.rel.x + ox, y = anchor.y + v.rel.y + oy,
				    z = anchor.z + v.rel.z + oz;
				if (!InBounds(x, y, z))
					continue;
				WriteVoxel(x, y, z, true, v.color);
				AddSelect(x, y, z); // select the pasted voxels
			}
			undo.End();
			RebuildRenderModel();
			SetStatus("Pasted " + std::to_string(clipboard.size()) + " voxels");
		}

		void KV6EditorView::CommitPaste() {
			PasteClipboard(pasteAnchor);
			pasteActive = false;
		}

		void KV6EditorView::DrawPastePreview() {
			for (const ClipVoxel& v : clipboard) {
				DrawCellOutline(pasteAnchor.x + v.rel.x, pasteAnchor.y + v.rel.y,
				                pasteAnchor.z + v.rel.z, ColorToVec(v.color));
			}
		}

		// --- Selection move (gizmo) ------------------------------------------

		bool KV6EditorView::SelectionCentroid(Vector3& out) const {
			if (selection.empty())
				return false;
			int minX = 1 << 30, minY = 1 << 30, minZ = 1 << 30;
			int maxX = -(1 << 30), maxY = -(1 << 30), maxZ = -(1 << 30);
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				minX = std::min(minX, x); minY = std::min(minY, y); minZ = std::min(minZ, z);
				maxX = std::max(maxX, x); maxY = std::max(maxY, y); maxZ = std::max(maxZ, z);
			}
			out = MakeVector3((minX + maxX) * 0.5F, (minY + maxY) * 0.5F, (minZ + maxZ) * 0.5F);
			return true;
		}

		void KV6EditorView::DrawSelectionOffset(int dx, int dy, int dz, const Vector4& color) {
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				DrawCellOutline(x + dx, y + dy, z + dz, color);
			}
		}

		void KV6EditorView::MoveSelection(int dx, int dy, int dz) {
			if (selection.empty() || (dx == 0 && dy == 0 && dz == 0))
				return;
			// Gather the selected voxels (with their colours).
			std::vector<ClipVoxel> moved;
			for (int64_t k : selection) {
				int x, y, z;
				SelDecode(k, x, y, z);
				if (InBounds(x, y, z) && model->IsSolid(x, y, z))
					moved.push_back({MakeIntVector3(x, y, z), model->GetColor(x, y, z) & 0xFFFFFF});
			}
			if (moved.empty())
				return;
			// Volume that must hold both the current model and the moved voxels at
			// their destination — checked up front so we can bail before editing.
			int loX = 0, loY = 0, loZ = 0;
			int hiX = model->GetWidth(), hiY = model->GetHeight(), hiZ = model->GetDepth();
			for (const ClipVoxel& v : moved) {
				int x = v.rel.x + dx, y = v.rel.y + dy, z = v.rel.z + dz;
				loX = std::min(loX, x); hiX = std::max(hiX, x + 1);
				loY = std::min(loY, y); hiY = std::max(hiY, y + 1);
				loZ = std::min(loZ, z); hiZ = std::max(hiZ, z + 1);
			}
			int nw = hiX - loX, nh = hiY - loY, nd = hiZ - loZ;
			if (nw > 4096 || nh > 4096 || nd > 64) {
				SetStatus("Reached the maximum model size");
				return;
			}
			undo.Begin("Move");
			// Lift the selected voxels off the model, then re-stamp them shifted.
			for (const ClipVoxel& v : moved)
				WriteVoxel(v.rel.x, v.rel.y, v.rel.z, false, 0);
			selection.clear();
			int ox = -loX, oy = -loY, oz = -loZ;
			if (ox != 0 || oy != 0 || oz != 0 || nw != model->GetWidth() ||
			    nh != model->GetHeight() || nd != model->GetDepth())
				RebuildVolume(nw, nh, nd, ox, oy, oz);
			for (const ClipVoxel& v : moved) {
				int x = v.rel.x + dx + ox, y = v.rel.y + dy + oy, z = v.rel.z + dz + oz;
				if (!InBounds(x, y, z))
					continue;
				WriteVoxel(x, y, z, true, v.color);
				AddSelect(x, y, z);
			}
			TrimVolume();
			undo.End();
			RebuildRenderModel();
		}

		// --- Layout / hit testing --------------------------------------------

		void KV6EditorView::LayoutPicker() {
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();
			float bottomClear = 44.0F;

			// Navigation cube: top-right corner of the viewport, just under the
			// toolbar/ribbon bars.
			float gizBox = 174.0F;
			gizR = gizBox * 0.5F - 14.0F;
			gizCx = sw - 16.0F - gizBox * 0.5F;
			gizCy = BarsH() + 8.0F + gizBox * 0.5F;

			presSwatch = (svSize + 6.0F + hueW) / float(presetCols);
			float contentW = svSize + 6.0F + hueW;
			prevH = 22.0F;
			float headerH = 18.0F; // title bar with the close button
			pkW = 8.0F * 2.0F + contentW;
			pkH = 8.0F * 2.0F + headerH + svSize + 6.0F + prevH + 6.0F + 2.0F * presSwatch;
			pkX = sw - 16.0F - pkW;
			pkY = sh - bottomClear - pkH; // picker now sits at the bottom-right
			closeS = 13.0F;
			closeX = pkX + pkW - 8.0F - closeS;
			closeY = pkY + 5.0F;
			svX = pkX + 8.0F;
			svY = pkY + 8.0F + headerH;
			hueX = svX + svSize + 6.0F;
			hueY = svY;
			prevY = svY + svSize + 6.0F;
			eyeS = prevH;
			prevX = svX;
			prevW = contentW - eyeS - 6.0F;
			eyeX = prevX + prevW + 6.0F;
			eyeY = prevY;
			presX = svX;
			presY = prevY + prevH + 6.0F;
		}

		bool KV6EditorView::CursorOverPicker(const Vector2& p) const {
			return pickerOpen && p.x >= pkX && p.x < pkX + pkW && p.y >= pkY && p.y < pkY + pkH;
		}

		bool KV6EditorView::InRect(const Vector2& p, float x, float y, float w, float h) const {
			return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
		}

		void KV6EditorView::UpdateSV(const Vector2& p) {
			sat = Clampf((p.x - svX) / svSize, 0.0F, 1.0F);
			val = Clampf(1.0F - (p.y - svY) / svSize, 0.0F, 1.0F);
			SyncColor();
		}
		void KV6EditorView::UpdateHue(const Vector2& p) {
			hue = Clampf((p.y - hueY) / svSize, 0.0F, 1.0F);
			SyncColor();
		}

		bool KV6EditorView::PickerMouseDown(const Vector2& p) {
			if (!pickerOpen)
				return false;
			if (InRect(p, closeX, closeY, closeS, closeS)) { pickerOpen = false; return true; }
			if (InRect(p, svX, svY, svSize, svSize)) { dragPick = 1; UpdateSV(p); return true; }
			if (InRect(p, hueX, hueY, hueW, svSize)) { dragPick = 2; UpdateHue(p); return true; }
			if (InRect(p, eyeX, eyeY, eyeS, eyeS)) {
				pickMode = !pickMode;
				SetStatus(pickMode ? "Pick mode: click a voxel" : "Pick mode off");
				return true;
			}
			for (size_t i = 0; i < presets.size(); i++) {
				float x = presX + float(int(i) % presetCols) * presSwatch;
				float y = presY + float(int(i) / presetCols) * presSwatch;
				if (InRect(p, x, y, presSwatch, presSwatch)) {
					currentColor = presets[i];
					RGBToHSV(currentColor);
					return true;
				}
			}
			return CursorOverPicker(p);
		}


		// --- Drawing primitives ----------------------------------------------

		void KV6EditorView::ColorNP(const Vector4& c) {
			renderer->SetColorAlphaPremultiplied(MakeVector4(c.x * c.w, c.y * c.w, c.z * c.w, c.w));
		}
		void KV6EditorView::FillRect(float x, float y, float w, float h) {
			renderer->DrawImage((client::IImage*)NULL, AABB2(x, y, w, h));
		}
		void KV6EditorView::StrokeRect(float x, float y, float w, float h, float t,
		                               const Vector4& c) {
			ColorNP(c);
			FillRect(x, y, w, t);
			FillRect(x, y + h - t, w, t);
			FillRect(x, y, t, h);
			FillRect(x + w - t, y, t, h);
		}
		void KV6EditorView::DrawLine2D(const Vector2& a, const Vector2& b, float w,
		                               const Vector4& col) {
			Vector2 d = b - a;
			float len = d.GetLength();
			if (len < 0.001F)
				return;
			Vector2 n = MakeVector2(-d.y, d.x) * (w * 0.5F / len);
			ColorNP(col);
			renderer->DrawImage((client::IImage*)NULL, a + n, b + n, a - n, AABB2(0, 0, 1, 1));
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
			Vector3 org = model->GetOrigin();
			for (int mx = 0; mx <= (MirrorOn(0) ? 1 : 0); mx++)
			for (int my = 0; my <= (MirrorOn(1) ? 1 : 0); my++)
			for (int mz = 0; mz <= (MirrorOn(2) ? 1 : 0); mz++) {
				IntVector3 a = lo, b = hi;
				if (mx) { int p = MirrorIdx(lo.x, -org.x), q = MirrorIdx(hi.x, -org.x);
				          a.x = std::min(p, q); b.x = std::max(p, q); }
				if (my) { int p = MirrorIdx(lo.y, -org.y), q = MirrorIdx(hi.y, -org.y);
				          a.y = std::min(p, q); b.y = std::max(p, q); }
				if (mz) { int p = MirrorIdx(lo.z, -org.z), q = MirrorIdx(hi.z, -org.z);
				          a.z = std::min(p, q); b.z = std::max(p, q); }
				DrawBoxOutline(a, b, color);
			}
		}

		void KV6EditorView::SelectBox(const IntVector3& lo, const IntVector3& hi) {
			undo.Begin("Select");
			for (int x = lo.x; x <= hi.x; x++)
			for (int y = lo.y; y <= hi.y; y++)
			for (int z = lo.z; z <= hi.z; z++) {
				if (InBounds(x, y, z) && model->IsSolid(x, y, z))
					AddSelect(x, y, z);
			}
			undo.End();
		}

		void KV6EditorView::SelectCells(const std::vector<IntVector3>& cells) {
			undo.Begin("Select");
			for (const IntVector3& c : cells) {
				if (InBounds(c.x, c.y, c.z) && model->IsSolid(c.x, c.y, c.z))
					AddSelect(c.x, c.y, c.z);
			}
			undo.End();
		}

		void KV6EditorView::FillCells(const std::vector<IntVector3>& cellsIn, uint32_t color) {
			std::vector<IntVector3> cells = cellsIn;
			ExpandMirrors(cells); // also fill the mirror images, if enabled
			if (cells.empty())
				return;
			// Grow the volume to contain every cell (plus the current model).
			int loX = 0, loY = 0, loZ = 0;
			int hiX = model->GetWidth(), hiY = model->GetHeight(), hiZ = model->GetDepth();
			for (const IntVector3& c : cells) {
				loX = std::min(loX, c.x); hiX = std::max(hiX, c.x + 1);
				loY = std::min(loY, c.y); hiY = std::max(hiY, c.y + 1);
				loZ = std::min(loZ, c.z); hiZ = std::max(hiZ, c.z + 1);
			}
			int nw = hiX - loX, nh = hiY - loY, nd = hiZ - loZ;
			if (nw > 4096 || nh > 4096 || nd > 64) {
				SetStatus("Reached the maximum model size");
				return;
			}
			int ox = -loX, oy = -loY, oz = -loZ;
			undo.Begin("Fill");
			if (ox != 0 || oy != 0 || oz != 0 || nw != model->GetWidth() ||
			    nh != model->GetHeight() || nd != model->GetDepth())
				RebuildVolume(nw, nh, nd, ox, oy, oz);
			bool any = false;
			for (const IntVector3& c : cells) {
				int X = c.x + ox, Y = c.y + oy, Z = c.z + oz;
				if (InBounds(X, Y, Z) && !model->IsSolid(X, Y, Z)) {
					WriteVoxel(X, Y, Z, true, color);
					any = true;
				}
			}
			undo.End();
			if (any)
				RebuildRenderModel();
		}

		void KV6EditorView::EraseCells(const std::vector<IntVector3>& cellsIn) {
			std::vector<IntVector3> cells = cellsIn;
			ExpandMirrors(cells); // also erase the mirror images, if enabled
			int count = 0;
			for (const IntVector3& c : cells) {
				if (InBounds(c.x, c.y, c.z) && model->IsSolid(c.x, c.y, c.z))
					count++;
			}
			if (count == 0)
				return;
			if (voxelCount - count < 1) {
				SetStatus("Cannot remove every voxel");
				return;
			}
			undo.Begin("Erase");
			for (const IntVector3& c : cells) {
				if (InBounds(c.x, c.y, c.z) && model->IsSolid(c.x, c.y, c.z)) {
					WriteVoxel(c.x, c.y, c.z, false, 0);
					selection.erase(SelKey(c.x, c.y, c.z));
				}
			}
			TrimVolume();
			undo.End();
			RebuildRenderModel();
		}

		void KV6EditorView::DeselectCells(const std::vector<IntVector3>& cells) {
			undo.Begin("Deselect");
			for (const IntVector3& c : cells)
				selection.erase(SelKey(c.x, c.y, c.z));
			undo.End();
		}

		void KV6EditorView::PaintCells(const std::vector<IntVector3>& cellsIn, uint32_t color) {
			std::vector<IntVector3> cells = cellsIn;
			ExpandMirrors(cells); // also recolour the mirror images, if enabled
			uint32_t rgb = color & 0xFFFFFF;
			undo.Begin("Paint");
			bool any = false;
			for (const IntVector3& c : cells) {
				if (!InBounds(c.x, c.y, c.z) || !model->IsSolid(c.x, c.y, c.z))
					continue; // paint only existing voxels; never grows the volume
				if ((model->GetColor(c.x, c.y, c.z) & 0xFFFFFF) == rgb)
					continue; // already this colour
				WriteVoxel(c.x, c.y, c.z, true, rgb);
				any = true;
			}
			undo.End();
			if (any)
				RebuildRenderModel();
		}

		void KV6EditorView::ApplyCells(const std::vector<IntVector3>& cells, bool secondary) {
			EditorTool* t = ActiveTool();
			EditorRole role = t ? t->Role() : EditorRole::Edit;
			if (role == EditorRole::Select) {
				if (secondary) DeselectCells(cells);
				else SelectCells(cells);
			} else if (role == EditorRole::Paint) {
				PaintCells(cells, currentColor); // no inverse: both buttons recolour
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
			bool oldSolid = model->IsSolid(x, y, z);
			uint32_t oldColor = model->GetColor(x, y, z) & 0xFFFFFF;
			uint32_t newColor = solid ? (color & 0xFFFFFF) : oldColor; // air keeps its colour
			WriteVoxelRaw(x, y, z, solid, newColor);
			undo.RecordVoxel(x, y, z, oldSolid, oldColor, solid, newColor);
		}

		void KV6EditorView::WriteVoxelRaw(int x, int y, int z, bool solid, uint32_t color) {
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

		// KV6UndoStack::Sink — the stack replays records through these.
		void KV6EditorView::UndoApplyVoxel(int x, int y, int z, bool solid, uint32_t color) {
			WriteVoxelRaw(x, y, z, solid, color);
		}
		void KV6EditorView::UndoApplyReframe(int w, int h, int d, int ox, int oy, int oz) {
			ReframeRaw(w, h, d, ox, oy, oz);
		}

		void KV6EditorView::Undo() {
			std::string label = undo.UndoLabel();
			if (undo.Undo())
				SetStatus("Undid " + (label.empty() ? std::string("edit") : label));
			else
				SetStatus("Nothing to undo");
		}
		void KV6EditorView::Redo() {
			std::string label = undo.RedoLabel();
			if (undo.Redo())
				SetStatus("Redid " + (label.empty() ? std::string("edit") : label));
			else
				SetStatus("Nothing to redo");
		}

		// --- Pivot ------------------------------------------------------------

		Vector3 KV6EditorView::GetPivot() const { return model->GetOrigin() * -1.0F; }

		// The renderer bakes `origin` into the render model, so re-bake after changing
		// it; that keeps the voxels visually fixed while the pivot marker moves.
		void KV6EditorView::ApplyOriginRaw(const Vector3& origin) {
			model->SetOrigin(origin);
			RebuildRenderModel();
		}

		void KV6EditorView::SetPivot(const Vector3& pivot) {
			Vector3 before = model->GetOrigin();
			Vector3 after = pivot * -1.0F;
			if (after.x == before.x && after.y == before.y && after.z == before.z)
				return;
			undo.Begin("Set Pivot");
			ApplyOriginRaw(after);
			undo.RecordOrigin(before, after);
			undo.End();
		}

		void KV6EditorView::UndoApplyOrigin(const Vector3& origin) { ApplyOriginRaw(origin); }

		// Live, non-journaled pivot move for a drag in progress; the tool commits the
		// net change with one SetPivot on release.
		void KV6EditorView::PreviewPivot(const Vector3& pivot) { ApplyOriginRaw(pivot * -1.0F); }

		void KV6EditorView::BeginPivotEntry() {
			Vector3 p = GetPivot();
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%.1f %.1f %.1f", p.x, p.y, p.z);
			promptText = buf;
			promptKind = PromptKind::Pivot;
			promptOpen = true;
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
			Vector4 grid = MakeVector4(1.0F, 1.0F, 1.0F, 0.06F);
			Vector4 box = MakeVector4(0.4F, 0.7F, 1.0F, 0.5F);
			float lo = -0.5F;
			float hiX = float(model->GetWidth()) - 0.5F;
			float hiY = float(model->GetHeight()) - 0.5F;
			float hiZ = float(model->GetDepth()) - 0.5F;

			for (int i = 0; i <= model->GetWidth(); i += 4)
				renderer->AddDebugLine(MakeVector3(float(i) - 0.5F, lo, hiZ),
				                       MakeVector3(float(i) - 0.5F, hiY, hiZ), grid);
			for (int i = 0; i <= model->GetHeight(); i += 4)
				renderer->AddDebugLine(MakeVector3(lo, float(i) - 0.5F, hiZ),
				                       MakeVector3(hiX, float(i) - 0.5F, hiZ), grid);

			Vector3 a = MakeVector3(lo, lo, lo);
			Vector3 b = MakeVector3(hiX, hiY, hiZ);
			BoxEdges(a, b, [&](const Vector3& p, const Vector3& q) {
				renderer->AddDebugLine(p, q, box);
			});
		}

		// Semi-transparent quad at each enabled mirror plane (drawn as a fan of
		// debug lines, since the editor only has line primitives in 3D).
		void KV6EditorView::DrawMirrorPlanes() {
			Vector3 org = model->GetOrigin();
			float lo = -0.5F;
			float hiX = float(model->GetWidth()) - 0.5F;
			float hiY = float(model->GetHeight()) - 0.5F;
			float hiZ = float(model->GetDepth()) - 0.5F;

			if (MirrorOn(0)) {
				float px = -org.x;
				Vector4 col = MakeVector4(1.0F, 0.35F, 0.35F, 0.25F);
				for (int i = 0; i <= model->GetHeight(); i += 2)
					renderer->AddDebugLine(MakeVector3(px, float(i) - 0.5F, lo),
					                       MakeVector3(px, float(i) - 0.5F, hiZ), col);
				for (int i = 0; i <= model->GetDepth(); i += 2)
					renderer->AddDebugLine(MakeVector3(px, lo, float(i) - 0.5F),
					                       MakeVector3(px, hiY, float(i) - 0.5F), col);
			}
			if (MirrorOn(1)) {
				float py = -org.y;
				Vector4 col = MakeVector4(0.4F, 1.0F, 0.4F, 0.25F);
				for (int i = 0; i <= model->GetWidth(); i += 2)
					renderer->AddDebugLine(MakeVector3(float(i) - 0.5F, py, lo),
					                       MakeVector3(float(i) - 0.5F, py, hiZ), col);
				for (int i = 0; i <= model->GetDepth(); i += 2)
					renderer->AddDebugLine(MakeVector3(lo, py, float(i) - 0.5F),
					                       MakeVector3(hiX, py, float(i) - 0.5F), col);
			}
			if (MirrorOn(2)) {
				float pz = -org.z;
				Vector4 col = MakeVector4(0.45F, 0.6F, 1.0F, 0.25F);
				for (int i = 0; i <= model->GetWidth(); i += 2)
					renderer->AddDebugLine(MakeVector3(float(i) - 0.5F, lo, pz),
					                       MakeVector3(float(i) - 0.5F, hiY, pz), col);
				for (int i = 0; i <= model->GetHeight(); i += 2)
					renderer->AddDebugLine(MakeVector3(lo, float(i) - 0.5F, pz),
					                       MakeVector3(hiX, float(i) - 0.5F, pz), col);
			}
		}

		void KV6EditorView::DrawPicker() {
			if (!pickerOpen)
				return;
			float pad = 2.0F;
			ColorNP(MakeVector4(0.0F, 0.0F, 0.0F, 0.55F));
			FillRect(pkX, pkY, pkW, pkH);

			// Header: "Colour" + close button.
			client::IFont& hf = fontManager->GetSmallGuiFont();
			hf.Draw("Colour", MakeVector2(pkX + 8.0F, pkY + 4.0F), 0.75F,
			        MakeVector4(0.85F, 0.85F, 0.85F, 1.0F));
			ColorNP(MakeVector4(0.35F, 0.18F, 0.18F, 1.0F));
			FillRect(closeX, closeY, closeS, closeS);
			StrokeRect(closeX, closeY, closeS, closeS, 1.0F, MakeVector4(0.7F, 0.5F, 0.5F, 0.9F));
			DrawLine2D(MakeVector2(closeX + 3.0F, closeY + 3.0F),
			           MakeVector2(closeX + closeS - 3.0F, closeY + closeS - 3.0F), 1.5F,
			           MakeVector4(1, 1, 1, 0.9F));
			DrawLine2D(MakeVector2(closeX + closeS - 3.0F, closeY + 3.0F),
			           MakeVector2(closeX + 3.0F, closeY + closeS - 3.0F), 1.5F,
			           MakeVector4(1, 1, 1, 0.9F));

			int cells = 24;
			float cw = svSize / float(cells);
			for (int yi = 0; yi < cells; yi++)
			for (int xi = 0; xi < cells; xi++) {
				float s = (float(xi) + 0.5F) / float(cells);
				float v = 1.0F - (float(yi) + 0.5F) / float(cells);
				ColorNP(ColorToVec(HSV(hue, s, v)));
				FillRect(svX + float(xi) * cw, svY + float(yi) * cw, cw + 0.5F, cw + 0.5F);
			}
			float mx = svX + sat * svSize;
			float my = svY + (1.0F - val) * svSize;
			ColorNP(MakeVector4(1.0F, 1.0F, 1.0F, 0.9F));
			FillRect(mx - 4.0F, my - 1.0F, 8.0F, 2.0F);
			FillRect(mx - 1.0F, my - 4.0F, 2.0F, 8.0F);

			int hcells = 24;
			float hh = svSize / float(hcells);
			for (int i = 0; i < hcells; i++) {
				ColorNP(ColorToVec(HSV((float(i) + 0.5F) / float(hcells), 1.0F, 1.0F)));
				FillRect(hueX, hueY + float(i) * hh, hueW, hh + 0.5F);
			}
			float hy = hueY + hue * svSize;
			ColorNP(MakeVector4(1.0F, 1.0F, 1.0F, 0.9F));
			FillRect(hueX - 2.0F, hy - 1.5F, hueW + 4.0F, 3.0F);

			ColorNP(ColorToVec(currentColor));
			FillRect(prevX, prevY, prevW, prevH);

			ColorNP(pickMode ? MakeVector4(0.18F, 0.45F, 0.24F, 1.0F)
			                 : MakeVector4(0.18F, 0.18F, 0.20F, 1.0F));
			FillRect(eyeX, eyeY, eyeS, eyeS);
			DrawLine2D(MakeVector2(eyeX + 5.0F, eyeY + eyeS - 5.0F),
			           MakeVector2(eyeX + eyeS - 5.0F, eyeY + 5.0F), 2.5F,
			           MakeVector4(1.0F, 1.0F, 1.0F, 0.9F));
			ColorNP(ColorToVec(currentColor));
			FillRect(eyeX + 4.0F, eyeY + eyeS - 8.0F, 4.0F, 4.0F);
			StrokeRect(eyeX, eyeY, eyeS, eyeS, pickMode ? 2.0F : 1.0F,
			           pickMode ? MakeVector4(0.5F, 1.0F, 0.6F, 1.0F)
			                    : MakeVector4(0.5F, 0.5F, 0.5F, 0.7F));

			for (size_t i = 0; i < presets.size(); i++) {
				float x = presX + float(int(i) % presetCols) * presSwatch;
				float y = presY + float(int(i) / presetCols) * presSwatch;
				ColorNP(ColorToVec(presets[i]));
				FillRect(x, y, presSwatch - pad, presSwatch - pad);
			}
		}

		// --- Navigation cube -------------------------------------------------

		void KV6EditorView::FillTri(const Vector2& A, const Vector2& B, const Vector2& C,
		                            const Vector4& col) {
			Vector2 p[3] = {A, B, C};
			auto sw = [](Vector2& a, Vector2& b) { Vector2 t = a; a = b; b = t; };
			if (p[0].y > p[1].y) sw(p[0], p[1]);
			if (p[1].y > p[2].y) sw(p[1], p[2]);
			if (p[0].y > p[1].y) sw(p[0], p[1]);
			float y0 = p[0].y, y2 = p[2].y;
			if (y2 - y0 < 0.5F)
				return;
			auto xAt = [](const Vector2& a, const Vector2& b, float y) {
				float dy = b.y - a.y;
				return a.x + (b.x - a.x) * ((std::fabs(dy) < 1.0e-5F) ? 0.0F : (y - a.y) / dy);
			};
			ColorNP(col);
			const int N = 10; // horizontal strips approximate the triangle
			for (int i = 0; i < N; i++) {
				float ya = y0 + (y2 - y0) * float(i) / N;
				float yb = y0 + (y2 - y0) * float(i + 1) / N;
				float la = xAt(p[0], p[2], ya), lb = xAt(p[0], p[2], yb);
				// The strip's lower-right corner is implied by DrawImage's affine
				// parallelogram (top-left, top-right, bottom-left), so only `ra` is
				// needed here.
				float ra = (ya < p[1].y) ? xAt(p[0], p[1], ya) : xAt(p[1], p[2], ya);
				renderer->DrawImage((client::IImage*)NULL, MakeVector2(la, ya), MakeVector2(ra, ya),
				                    MakeVector2(lb, yb), AABB2(0, 0, 1, 1));
			}
		}

		// A solid, shaded cube drawn as a 2D overlay: project the camera-facing faces
		// and fill them (used for the move gizmo's draggable handles).
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
				Vector3 toCam = camEye - (center + n * half);
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
				if (-Vector3::Dot(f.dir, camFwd) <= 0.02F)
					continue; // back-facing
				Vector2 q[4];
				for (int i = 0; i < f.n; i++)
					q[i] = MakeVector2(gizCx + Vector3::Dot(f.v[i], camRight) * gizR,
					                   gizCy - Vector3::Dot(f.v[i], camUp) * gizR);
				if (PointInPoly(p, q, f.n)) { dir = f.dir; return true; }
			}
			return false;
		}

		void KV6EditorView::DrawNaviCube() {
			client::IFont& font = fontManager->GetSmallGuiFont();
			const std::vector<NaviFacet>& facets = NaviFacets();
			Vector3 hdir;
			bool hov = NaviCubeDir(cursor, hdir);
			// Draw bevels behind the faces: corners, then edges, then faces.
			for (int pass = 0; pass < 3; pass++) {
				for (const NaviFacet& f : facets) {
					bool isFace = f.tint >= 0;
					bool isCorner = (f.n == 3);
					bool isEdge = !isFace && !isCorner;
					if ((pass == 0 && !isCorner) || (pass == 1 && !isEdge) || (pass == 2 && !isFace))
						continue;
					float facing = -Vector3::Dot(f.dir, camFwd);
					if (facing <= 0.02F)
						continue;
					Vector2 q[4];
					for (int i = 0; i < f.n; i++)
						q[i] = MakeVector2(gizCx + Vector3::Dot(f.v[i], camRight) * gizR,
						                   gizCy - Vector3::Dot(f.v[i], camUp) * gizR);
					float sh = 0.45F + 0.55F * facing;
					Vector4 base = isFace ? AxisTint(f.tint) : MakeVector4(0.5F, 0.52F, 0.56F, 1.0F);
					bool hl = hov && Vector3::Dot(hdir, f.dir) > 0.999F;
					Vector4 col = hl ? MakeVector4(0.4F, 0.7F, 1.0F, 0.97F)
					                 : MakeVector4(base.x * sh, base.y * sh, base.z * sh, 0.97F);
					if (f.n == 3) {
						FillTri(q[0], q[1], q[2], col);
					} else {
						ColorNP(col);
						renderer->DrawImage((client::IImage*)NULL, q[0], q[1], q[3], AABB2(0, 0, 1, 1));
					}
					Vector4 ec = MakeVector4(0.08F, 0.08F, 0.1F, 0.85F);
					for (int e = 0; e < f.n; e++)
						DrawLine2D(q[e], q[(e + 1) % f.n], 1.0F, ec);
				}
			}
			// Face labels last, so they sit on top of the cube.
			for (const NaviFacet& f : facets) {
				if (!f.label || -Vector3::Dot(f.dir, camFwd) <= 0.02F)
					continue;
				Vector2 ctr = MakeVector2(0.0F, 0.0F);
				for (int i = 0; i < 4; i++)
					ctr += MakeVector2(gizCx + Vector3::Dot(f.v[i], camRight) * gizR,
					                   gizCy - Vector3::Dot(f.v[i], camUp) * gizR);
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
			orbitMode = true; // navicube clicks orbit around the model
		}

		void KV6EditorView::DrawOverlay(float sw, float sh) {
			(void)sw;
			client::IFont& font = fontManager->GetSmallGuiFont();
			Vector4 grey = MakeVector4(0.75F, 0.75F, 0.75F, 1.0F);

			// Title / filename / camera now live in the ribbon. Here we draw the
			// transient status line (above the help line) and the help line.
			if (statusTimer > 0.0F)
				font.Draw(statusMessage, MakeVector2(16.0F, sh - 50.0F), 1.0F,
				          MakeVector4(0.5F, 1.0F, 0.6F, 1.0F));

			font.Draw("[LMB] use tool  |  [RMB] delete/cancel  |  [MMB] look  |  [WASD/Space/Ctrl] move"
			          "  |  [Wheel] zoom  |  [Ctrl+C/X/V] copy/cut/paste  |  [Ctrl+Z/Y] undo/redo"
			          "  |  [Esc] menu",
			          MakeVector2(16.0F, sh - 28.0F), 1.0F, grey);
		}

		void KV6EditorView::DrawCursor() {
			ColorNP(MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
			if (cursorImg)
				renderer->DrawImage(cursorImg.GetPointerOrNull(),
				                    MakeVector2(cursor.x - 8.0F, cursor.y - 8.0F));
		}

		EditorTool* KV6EditorView::ActiveTool() {
			if (currentMode == EditorMode::Edit && activeTool >= 0 && activeTool < int(tools.size()))
				return tools[activeTool].get();
			return nullptr;
		}

		PointerInput KV6EditorView::MakePointer(PointerButton b, PointerPhase ph,
		                                        const Vector2& delta) const {
			PointerInput e;
			e.button = b;
			e.phase = ph;
			e.pos = cursor;
			e.delta = delta;
			e.alt = altHeld;
			e.ctrl = ctrlHeld;
			e.shift = shiftHeld;
			return e;
		}

		void KV6EditorView::DispatchPointer(const PointerInput& e) {
			if (EditorTool* t = ActiveTool())
				t->OnPointer(*this, e);
		}

		// --- Ribbon (title) + unified toolbar [modes] | [tools] -------------
		//
		// Two stacked full-width bars at the very top: a ribbon (title/filename)
		// above a left-aligned toolbar. The 3D viewport is drawn below them.

		static const char* kModeNames[3] = {"Object", "Edit", "Animation"};

		// X of toolbar slot `i` (modes 0..2, then a separator, then tools 3..).
		static float ToolbarX(int slot, int toolCount) {
			float x = kTbX0 + float(slot) * (kTbBtn + kTbGap);
			if (slot >= 3 && toolCount > 0)
				x += kTbSep;
			return x;
		}

		float KV6EditorView::UndoButtonX(float sw, bool redo) const {
			float undoX = sw - 12.0F - 2.0F * kUndoBtnW - kTbGap;
			return redo ? undoX + kUndoBtnW + kTbGap : undoX;
		}

		KV6EditorView::ToolbarHit KV6EditorView::ToolbarHitTest(const Vector2& p) {
			int toolCount = (currentMode == EditorMode::Edit) ? int(tools.size()) : 0;
			for (int i = 0; i < 3; i++) {
				if (InRect(p, ToolbarX(i, toolCount), kTbY, kTbBtn, kTbH))
					return {ToolbarHit::Mode, i};
			}
			for (int i = 0; i < toolCount; i++) {
				if (InRect(p, ToolbarX(3 + i, toolCount), kTbY, kTbBtn, kTbH))
					return {ToolbarHit::Tool, i};
			}
			float sw = renderer->ScreenWidth();
			if (InRect(p, UndoButtonX(sw, false), kTbY, kUndoBtnW, kTbH))
				return {ToolbarHit::Undo, 0};
			if (InRect(p, UndoButtonX(sw, true), kTbY, kUndoBtnW, kTbH))
				return {ToolbarHit::Redo, 0};
			return {};
		}

		void KV6EditorView::DrawToolbar(float sw, float sh) {
			(void)sh;
			client::IFont& font = fontManager->GetSmallGuiFont();
			float s = 0.85F;
			int toolCount = (currentMode == EditorMode::Edit) ? int(tools.size()) : 0;

			// Full-width toolbar band.
			ColorNP(MakeVector4(0.10F, 0.10F, 0.12F, 1.0F));
			FillRect(0.0F, kRibbonH, sw, kToolbarH);

			// Buttons share the game's look/behaviour via the C++ painter; `active`
			// maps to the toggled state and the cursor drives hover, so they highlight
			// exactly like the in-game buttons.
			auto button = [&](float x, const char* label, bool active, bool enabled) {
				bool hover = !menuOpen && !promptOpen && InRect(cursor, x, kTbY, kTbBtn, kTbH);
				widgets::PaintButton(*renderer, font, MakeVector2(x, kTbY),
				                     MakeVector2(kTbBtn, kTbH), label, MakeVector2(0.5F, 0.5F), "",
				                     MakeVector2(1.0F, 0.5F), enabled, hover, false, active, s);
			};

			for (int i = 0; i < 3; i++)
				button(ToolbarX(i, toolCount), kModeNames[i], int(currentMode) == i, i == 1);

			if (toolCount > 0) {
				float sx = ToolbarX(3, toolCount) - kTbSep * 0.5F - kTbGap;
				ColorNP(MakeVector4(0.5F, 0.5F, 0.5F, 0.5F));
				FillRect(sx, kTbY + 3.0F, 1.0F, kTbH - 6.0F);
				for (int i = 0; i < toolCount; i++)
					button(ToolbarX(3 + i, toolCount), tools[i]->Label(), activeTool == i, true);
			}

			// Undo / Redo on the right edge, greyed out when there's nothing to do.
			auto urButton = [&](float x, const char* label, bool enabled) {
				bool hover = !menuOpen && !promptOpen && InRect(cursor, x, kTbY, kUndoBtnW, kTbH);
				widgets::PaintButton(*renderer, font, MakeVector2(x, kTbY),
				                     MakeVector2(kUndoBtnW, kTbH), label, MakeVector2(0.5F, 0.5F), "",
				                     MakeVector2(1.0F, 0.5F), enabled, hover, false, false, s);
			};
			urButton(UndoButtonX(sw, false), "Undo", undo.CanUndo());
			urButton(UndoButtonX(sw, true), "Redo", undo.CanRedo());
		}

		bool KV6EditorView::MirrorOn(int axis) const {
			if (axis < 0 || axis > 2)
				return false;
			if (currentMode != EditorMode::Edit || activeTool < 0 ||
			    activeTool >= int(tools.size()))
				return false;
			ToolOptions* o = tools[activeTool]->Options();
			if (!o)
				return false;
			static const char* ids[3] = {"mirror.x", "mirror.y", "mirror.z"};
			return o->GetBool(ids[axis]);
		}

		int KV6EditorView::SubToolbarHitTest(const Vector2& p) {
			EditorTool* t = ActiveTool();
			if (!t || t->SubToolCount() == 0)
				return -1;
			float by = kRibbonH + kToolbarH + (kSubBarH - kTbH) * 0.5F;
			for (int i = 0; i < t->SubToolCount(); i++) {
				if (InRect(p, kTbX0 + float(i) * (kSubBtn + kTbGap), by, kSubBtn, kTbH))
					return i;
			}
			return -1;
		}

		// X (and width via outW) of the active tool's option `i`, laid out after the
		// sub-tool buttons. Options in a new group are preceded by a separator and,
		// if the group is named, room for its label.
		float KV6EditorView::OptionRect(int i, float& outW) {
			outW = 0.0F;
			EditorTool* t = ActiveTool();
			ToolOptions* o = t ? t->Options() : nullptr;
			if (!o)
				return 0.0F;
			float x = kTbX0 + float(t->SubToolCount()) * (kSubBtn + kTbGap);
			std::string prevGroup;
			bool first = true;
			for (int k = 0; k < o->Count(); k++) {
				const ToolOption& op = o->At(k);
				bool newGroup = first || op.group != prevGroup;
				if (newGroup) {
					x += kTbSep; // separator before a new group
					if (!op.group.empty())
						x += kMirLabelW; // room for the group label
				} else {
					x += kTbGap; // gap between items in the same group
				}
				float w = (op.type == ToolOption::Type::Color)
				            ? kColorW
				            : (op.type == ToolOption::Type::Label ? kLabelW : kMirW);
				if (k == i) {
					outW = w;
					return x;
				}
				x += w;
				prevGroup = op.group;
				first = false;
			}
			return x;
		}

		void KV6EditorView::DrawSubToolbar(float sw) {
			client::IFont& font = fontManager->GetSmallGuiFont();
			float s = 0.8F;
			float bandY = kRibbonH + kToolbarH;
			float by = bandY + (kSubBarH - kTbH) * 0.5F;
			ColorNP(MakeVector4(0.08F, 0.08F, 0.10F, 1.0F));
			FillRect(0.0F, bandY, sw, kSubBarH); // band is always present
			EditorTool* t = ActiveTool();
			if (!t)
				return;
			for (int i = 0; i < t->SubToolCount(); i++) {
				float x = kTbX0 + float(i) * (kSubBtn + kTbGap);
				bool on = t->ActiveSubTool() == i;
				bool hover = !menuOpen && !promptOpen && InRect(cursor, x, by, kSubBtn, kTbH);
				widgets::PaintButton(*renderer, font, MakeVector2(x, by), MakeVector2(kSubBtn, kTbH),
				                     t->SubToolLabel(i), MakeVector2(0.5F, 0.5F), "",
				                     MakeVector2(1.0F, 0.5F), true, hover, false, on, s);
			}

			// The tool's declarative options (toggles, colour swatch), laid out
			// generically after the sub-tool buttons.
			ToolOptions* opts = t->Options();
			if (!opts)
				return;
			std::string prevGroup;
			bool first = true;
			for (int i = 0; i < opts->Count(); i++) {
				const ToolOption& op = opts->At(i);
				float w;
				float x = OptionRect(i, w);
				bool newGroup = first || op.group != prevGroup;
				if (newGroup) {
					// Separator before the group; named groups also get a label.
					float labelW = op.group.empty() ? 0.0F : kMirLabelW;
					ColorNP(MakeVector4(0.5F, 0.5F, 0.5F, 0.4F));
					FillRect(x - labelW - kTbSep * 0.5F, by + 2.0F, 1.0F, kTbH - 4.0F);
					if (!op.group.empty())
						font.Draw(op.group,
						          MakeVector2(x - kMirLabelW + 2.0F, by + (kTbH - 9.0F * s) * 0.5F), s,
						          MakeVector4(0.75F, 0.75F, 0.75F, 1.0F));
				}
				if (op.type == ToolOption::Type::Label) {
					Vector2 ts = font.Measure(op.label);
					font.Draw(op.label, MakeVector2(x, by + (kTbH - ts.y * s) * 0.5F), s,
					          MakeVector4(0.85F, 0.85F, 0.9F, 1.0F));
				} else if (op.type == ToolOption::Type::Color) {
					ColorNP(ColorToVec(currentColor));
					FillRect(x, by, w, kTbH);
					StrokeRect(x, by, w, kTbH, pickerOpen ? 2.0F : 1.0F,
					           pickerOpen ? MakeVector4(0.5F, 0.8F, 1.0F, 1.0F)
					                      : MakeVector4(0.8F, 0.8F, 0.8F, 0.7F));
				} else { // Bool toggle -> shared button painter, toggled when on
					bool hover = !menuOpen && !promptOpen && InRect(cursor, x, by, w, kTbH);
					widgets::PaintButton(*renderer, font, MakeVector2(x, by), MakeVector2(w, kTbH),
					                     op.label, MakeVector2(0.5F, 0.5F), "",
					                     MakeVector2(1.0F, 0.5F), true, hover, false, op.bvalue, s);
				}
				prevGroup = op.group;
				first = false;
			}
		}

		int KV6EditorView::SubToolbarOptionAt(const Vector2& p) {
			EditorTool* t = ActiveTool();
			ToolOptions* o = t ? t->Options() : nullptr;
			if (!o)
				return -1;
			float by = kRibbonH + kToolbarH + (kSubBarH - kTbH) * 0.5F;
			for (int i = 0; i < o->Count(); i++) {
				float w;
				float x = OptionRect(i, w);
				if (InRect(p, x, by, w, kTbH))
					return i;
			}
			return -1;
		}

		void KV6EditorView::DrawRibbon(float sw) {
			client::IFont& font = fontManager->GetSmallGuiFont();
			ColorNP(MakeVector4(0.06F, 0.06F, 0.08F, 1.0F));
			FillRect(0.0F, 0.0F, sw, kRibbonH);

			std::string name = (!filePath.empty()) ? filePath : "(unsaved model)";
			if (IsDirty())
				name += " *";
			font.Draw("KV6 Editor", MakeVector2(12.0F, 4.0F), 0.95F, MakeVector4(1, 1, 1, 1));
			font.Draw(name + "   (" + std::to_string(voxelCount) + " voxels)",
			          MakeVector2(120.0F, 5.0F), 0.85F, MakeVector4(0.75F, 0.75F, 0.78F, 1.0F));

			std::string cam = std::string(orbitMode ? "Orbit" : "Free-fly") +
			                  "   [Tab] camera   [Ctrl+S] save";
			Vector2 cs = font.Measure(cam);
			font.Draw(cam, MakeVector2(sw - 12.0F - cs.x * 0.8F, 5.0F), 0.8F,
			          MakeVector4(0.6F, 0.6F, 0.63F, 1.0F));
		}

		// --- Pause menu / Save As prompt -------------------------------------

		static const char* kMenuItems[4] = {"Resume", "Save", "Save As...", "Exit to Menu"};

		int KV6EditorView::MenuButtonAt(const Vector2& p) {
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();
			float w = 260.0F;
			float x = (sw - w) * 0.5F;
			float y = sh * 0.5F - 110.0F + 44.0F;
			for (int i = 0; i < 4; i++) {
				if (InRect(p, x, y, w, 36.0F))
					return i;
				y += 44.0F;
			}
			return -1;
		}

		void KV6EditorView::DrawMenu(float sw, float sh) {
			client::IFont& font = fontManager->GetSmallGuiFont();
			ColorNP(MakeVector4(0.0F, 0.0F, 0.0F, 0.7F));
			FillRect(0, 0, sw, sh);

			float w = 260.0F;
			float x = (sw - w) * 0.5F;
			float y = sh * 0.5F - 110.0F;

			Vector2 sz = font.Measure("KV6 Editor");
			font.Draw("KV6 Editor", MakeVector2(x + (w - sz.x) * 0.5F, y), 1.0F,
			          MakeVector4(1, 1, 1, 1));
			y += 44.0F;
			int hover = MenuButtonAt(cursor);
			for (int i = 0; i < 4; i++) {
				widgets::PaintButton(*renderer, font, MakeVector2(x, y), MakeVector2(w, 36.0F),
				                     kMenuItems[i], MakeVector2(0.5F, 0.5F), "",
				                     MakeVector2(1.0F, 0.5F), true, hover == i, false, false);
				y += 44.0F;
			}
		}

		void KV6EditorView::DrawPrompt(float sw, float sh) {
			client::IFont& font = fontManager->GetSmallGuiFont();
			ColorNP(MakeVector4(0.0F, 0.0F, 0.0F, 0.7F));
			FillRect(0, 0, sw, sh);

			float w = 460.0F, h = 116.0F;
			float x = (sw - w) * 0.5F, y = (sh - h) * 0.5F;
			ColorNP(MakeVector4(0.16F, 0.16F, 0.18F, 1.0F));
			FillRect(x, y, w, h);
			StrokeRect(x, y, w, h, 1.0F, MakeVector4(0.5F, 0.5F, 0.5F, 0.7F));

			const char* title =
			  (promptKind == PromptKind::Pivot) ? "Set pivot (x y z)" : "Save As (full path)";
			font.Draw(title, MakeVector2(x + 16.0F, y + 12.0F), 1.0F,
			          MakeVector4(0.8F, 0.8F, 0.8F, 1.0F));

			float fx = x + 16.0F, fy = y + 44.0F, fw = w - 32.0F, fh = 28.0F;
			widgets::PaintField(*renderer, MakeVector2(fx, fy), MakeVector2(fw, fh), true, false);
			std::string shown = promptText + "_";
			font.Draw(shown, MakeVector2(fx + 6.0F, fy + 6.0F), 1.0F, MakeVector4(1, 1, 1, 1));

			font.Draw("[Enter] OK    [Esc] cancel", MakeVector2(x + 16.0F, y + h - 24.0F), 0.9F,
			          MakeVector4(0.7F, 0.7F, 0.7F, 1.0F));
		}

		void KV6EditorView::SubmitPrompt() {
			if (promptKind == PromptKind::SaveAs) {
				std::string p = promptText;
				if (!p.empty()) {
					if (p.size() < 4 || !EqualsIgnoringCase(p.substr(p.size() - 4), ".kv6"))
						p += ".kv6";
					filePath = p;
					Save();
				}
				promptOpen = false;
				menuOpen = false;
				return;
			}
			// Pivot: parse three numbers (commas allowed).
			std::string s = promptText;
			for (char& c : s)
				if (c == ',')
					c = ' ';
			float x, y, z;
			if (std::sscanf(s.c_str(), "%f %f %f", &x, &y, &z) == 3) {
				SetPivot(MakeVector3(x, y, z));
				SetStatus("Pivot set");
			} else {
				SetStatus("Enter three numbers: x y z");
			}
			promptOpen = false;
		}

		// --- View interface ---------------------------------------------------

		void KV6EditorView::MouseEvent(float dx, float dy) {
			if (lookActive) {
				camAnim = false; // manual look cancels a navicube animation
				float sens = 0.003F;
				yaw += dx * sens;
				pitch -= dy * sens;
				float lim = M_PI_F * 0.5F - 0.01F;
				pitch = Clampf(pitch, -lim, lim);
				return;
			}
			cursor.x = Clampf(cursor.x + dx, 0.0F, renderer->ScreenWidth());
			cursor.y = Clampf(cursor.y + dy, 0.0F, renderer->ScreenHeight());
			if (dragPick == 1) { UpdateSV(cursor); return; }
			if (dragPick == 2) { UpdateHue(cursor); return; }

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
			if (menuOpen || promptOpen)
				return;
			if (orbitMode)
				orbitDist = Clampf(orbitDist * (1.0F + y * 0.1F), 2.0F, 1000.0F);
			else
				freePos += Forward() * (-y * float(cubeSize) * 0.1F);
		}

		void KV6EditorView::KeyEvent(const std::string& key, bool down) {
			// Save As prompt takes priority over everything.
			if (promptOpen) {
				if (!down)
					return;
				if (key == "Escape") { promptOpen = false; }
				else if (key == "Enter") { SubmitPrompt(); }
				else if (key == "BackSpace") {
					if (!promptText.empty())
						promptText.pop_back();
				}
				return;
			}

			if (menuOpen) {
				if (!down)
					return;
				if (key == "Escape") { menuOpen = false; return; }
				if (key == "LeftMouseButton") {
					int b = MenuButtonAt(cursor);
					if (b == 0) menuOpen = false;            // Resume
					else if (b == 1) { Save(); menuOpen = false; } // Save
					else if (b == 2) { // Save As
						promptText = filePath;
						promptKind = PromptKind::SaveAs;
						promptOpen = true;
					}
					else if (b == 3) wantsClose = true;      // Exit
				}
				return;
			}

			// While pasting, the mouse positions/places the clipboard; other keys
			// (camera) fall through.
			if (pasteActive && down) {
				if (key == "Escape") { pasteActive = false; SetStatus("Paste cancelled"); return; }
				if (key == "RightMouseButton") { pasteActive = false; return; }
				if (key == "LeftMouseButton") { CommitPaste(); return; }
			}

			if (down && key == "Escape") {
				// Let the active tool abort an in-progress operation first; only
				// open the pause menu when there's nothing to cancel.
				if (EditorTool* t = ActiveTool()) {
					if (t->OnEscape(*this))
						return;
				}
				menuOpen = true;
				return;
			}

			if (key == "Control") ctrlHeld = down;
			if (key == "Alt") altHeld = down;
			if (key == "Shift") shiftHeld = down;
			if (down && ctrlHeld && EqualsIgnoringCase(key, "s")) { Save(); return; }
			if (down && ctrlHeld && EqualsIgnoringCase(key, "c")) { CopySelection(); return; }
			if (down && ctrlHeld && EqualsIgnoringCase(key, "x")) { CutSelection(); return; }
			if (down && ctrlHeld && EqualsIgnoringCase(key, "v")) { StartPaste(); return; }
			if (down && ctrlHeld && EqualsIgnoringCase(key, "z")) {
				if (shiftHeld) Redo(); else Undo();
				return;
			}
			if (down && ctrlHeld && EqualsIgnoringCase(key, "y")) { Redo(); return; }

			if (key == "MiddleMouseButton") { lookActive = down; return; }

			if (key == "LeftMouseButton") {
				if (!down) {
					dragPick = 0;
					lmbHeld = false;
					DispatchPointer(MakePointer(PointerButton::Left, PointerPhase::Up));
					return;
				}
				ToolbarHit hit = ToolbarHitTest(cursor);
				if (hit.kind == ToolbarHit::Mode) {
					if (hit.index != 1)
						SetStatus("Only Edit mode is available for KV6 models");
					return;
				}
				if (hit.kind == ToolbarHit::Tool) {
					if (hit.index != activeTool) {
						if (EditorTool* t = ActiveTool()) t->OnDeactivate(*this);
						activeTool = hit.index;
						if (EditorTool* t = ActiveTool()) t->OnActivate(*this);
					}
					return;
				}
				if (hit.kind == ToolbarHit::Undo) { Undo(); return; }
				if (hit.kind == ToolbarHit::Redo) { Redo(); return; }
				int sub = SubToolbarHitTest(cursor);
				if (sub >= 0) {
					if (EditorTool* t = ActiveTool()) t->SetSubTool(*this, sub);
					return;
				}
				int opt = SubToolbarOptionAt(cursor);
				if (opt >= 0) {
					ToolOptions* o = ActiveTool() ? ActiveTool()->Options() : nullptr;
					if (o) {
						ToolOption& op = o->At(opt);
						if (op.type == ToolOption::Type::Color)
							pickerOpen = !pickerOpen; // open/close the colour picker
						else if (op.type == ToolOption::Type::Bool)
							op.bvalue = !op.bvalue; // toggle (e.g. a mirror axis)
						// Label: read-only readout, nothing to do
					}
					return;
				}
				if (PickerMouseDown(cursor)) return;
				Vector3 navDir;
				if (NaviCubeDir(cursor, navDir)) { SnapCameraDir(navDir); return; } // snap view
				if (cursor.y < BarsH()) return; // over the ribbon/toolbar bars
				lmbHeld = true;
				DispatchPointer(MakePointer(PointerButton::Left, PointerPhase::Down));
				return;
			}
			if (key == "RightMouseButton") {
				if (!down) {
					rmbHeld = false;
					DispatchPointer(MakePointer(PointerButton::Right, PointerPhase::Up));
					return;
				}
				if (CursorOverPicker(cursor)) return;
				if (cursor.y < BarsH()) return; // over the bars
				rmbHeld = true;
				DispatchPointer(MakePointer(PointerButton::Right, PointerPhase::Down));
				return;
			}

			if (down && key == "Tab") { ToggleCameraMode(); return; }

			std::string fwd = cg_keyMoveForward, bk = cg_keyMoveBackward, lf = cg_keyMoveLeft;
			std::string rt = cg_keyMoveRight, jp = cg_keyJump, cr = cg_keyCrouch;
			if (KV6CheckKey(fwd, key)) { keyFwd = down; return; }
			if (KV6CheckKey(bk, key)) { keyBack = down; return; }
			if (KV6CheckKey(lf, key)) { keyLeft = down; return; }
			if (KV6CheckKey(rt, key)) { keyRight = down; return; }
			if (KV6CheckKey(jp, key)) { keyUp = down; return; }
			if (KV6CheckKey(cr, key)) { keyDown = down; return; }

			// Remaining keys go to the active tool (e.g. Select's [L]).
			if (EditorTool* t = ActiveTool()) {
				KeyInput e;
				e.key = key;
				e.phase = down ? KeyPhase::Down : KeyPhase::Up;
				e.alt = altHeld;
				e.ctrl = ctrlHeld;
				e.shift = shiftHeld;
				t->OnKey(*this, e);
			}
		}

		void KV6EditorView::TextInputEvent(const std::string& text) {
			if (promptOpen)
				promptText += text;
		}
		bool KV6EditorView::AcceptsTextInput() { return promptOpen; }
		AABB2 KV6EditorView::GetTextInputRect() {
			float sw = renderer->ScreenWidth(), sh = renderer->ScreenHeight();
			float w = 460.0F, h = 116.0F;
			float x = (sw - w) * 0.5F, y = (sh - h) * 0.5F;
			return AABB2(x + 16.0F, y + 44.0F, w - 32.0F, 28.0F);
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

			if (!menuOpen && !promptOpen)
				UpdateMovement(dt);
			if (statusTimer > 0.0F)
				statusTimer -= dt;

			renderer->SetFogColor(MakeVector3(0.10F, 0.10F, 0.12F));
			renderer->SetFogDistance(1000.0F);

			// The scene is rendered full-screen (sub-viewport rendering isn't
			// guaranteed across renderers — keep this renderer-agnostic), and the
			// ribbon + toolbar bars are drawn opaque over the top. So the camera
			// projection and the cursor->ray pick both use the full screen.
			client::SceneDefinition sceneDef = SetupScene(0.0F, 0.0F, sw, sh);
			camEye = sceneDef.viewOrigin;
			camRight = sceneDef.viewAxis[0];
			camUp = sceneDef.viewAxis[1];
			camFwd = sceneDef.viewAxis[2];
			camFovX = sceneDef.fovX;
			camFovY = sceneDef.fovY;
			camVpX = 0.0F;
			camVpY = 0.0F;
			camSW = sw;
			camSH = sh;

			renderer->StartScene(sceneDef);
			if (renderModel) {
				client::ModelRenderParam param;
				// Cancel the KV6 pivot so the model sits in the editor's grid space.
				param.matrix = Matrix4::Translate(model->GetOrigin() * -1.0F);
				renderer->RenderModel(*renderModel, param);
			}
			DrawHelpers();
			DrawOriginAxes();
			DrawMirrorPlanes();
			DrawSelection();

			EditorTool* tool = ActiveTool();
			if (pasteActive) {
				DoPick();
				if (pickHit)
					pasteAnchor = MakeIntVector3(pickHX, pickHY, pickHZ); // follow the cursor
				DrawPastePreview();
			} else if (tool) {
				tool->DrawScene(*this);
			}
			renderer->EndScene();

			DrawOverlayLines2D(); // dim see-through pass for occluded outlines/gizmo
			DrawOverlay(sw, sh);
			DrawRibbon(sw);
			DrawToolbar(sw, sh);
			DrawSubToolbar(sw);
			LayoutPicker();
			DrawNaviCube();
			DrawPicker();
			if (tool)
				tool->DrawOverlay(*this);

			if (menuOpen)
				DrawMenu(sw, sh);
			if (promptOpen)
				DrawPrompt(sw, sh);

			DrawCursor();
		}

		void KV6EditorView::RunFrameLate(float dt) {
			SPADES_MARK_FUNCTION();
			(void)dt;
			renderer->FrameDone();
			renderer->Flip();
		}
	} // namespace gui
} // namespace spades
