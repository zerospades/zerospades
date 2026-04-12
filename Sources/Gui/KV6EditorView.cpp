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
#include "KV6ScreenHelper.h"

#include <algorithm>
#include <cmath>

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
			dirty = true;
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
			dirty = false;
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
				dirty = false;
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

		client::SceneDefinition KV6EditorView::SetupScene(float sw, float sh) {
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
			sceneDef.fovX = 2.0F * atanf(tanf(sceneDef.fovY * 0.5F) * (sw / sh));
			sceneDef.zNear = 0.1F;
			sceneDef.zFar = 1000.0F;
			sceneDef.viewportLeft = 0;
			sceneDef.viewportTop = 0;
			sceneDef.viewportWidth = int(sw);
			sceneDef.viewportHeight = int(sh);
			sceneDef.skipWorld = true;
			sceneDef.denyCameraBlur = true;
			sceneDef.time = (unsigned int)(globalTime * 1000.0F);
			return sceneDef;
		}

		// --- Editing ----------------------------------------------------------

		void KV6EditorView::DoPick() {
			pickHit = false;
			if (camSW <= 0.0F || camSH <= 0.0F)
				return;

			float sx = (cursor.x / camSW) * 2.0F - 1.0F;
			float sy = (cursor.y / camSH) * 2.0F - 1.0F;
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

		void KV6EditorView::RebuildVolume(int nw, int nh, int nd, int ox, int oy, int oz) {
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
			int xb = mirrorX ? MirrorIdx(tx, -org.x) : tx; int nx = (mirrorX && xb != tx) ? 2 : 1;
			int yb = mirrorY ? MirrorIdx(ty, -org.y) : ty; int ny = (mirrorY && yb != ty) ? 2 : 1;
			int zb = mirrorZ ? MirrorIdx(tz, -org.z) : tz; int nz = (mirrorZ && zb != tz) ? 2 : 1;

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
			if (ox != 0 || oy != 0 || oz != 0 || nw != model->GetWidth() ||
			    nh != model->GetHeight() || nd != model->GetDepth())
				RebuildVolume(nw, nh, nd, ox, oy, oz);
			tx += ox; xb += ox; ty += oy; yb += oy; tz += oz; zb += oz;

			bool any = false;
			for (int ia = 0; ia < nx; ia++) { int X = (ia == 0) ? tx : xb;
			for (int ib = 0; ib < ny; ib++) { int Y = (ib == 0) ? ty : yb;
			for (int ic = 0; ic < nz; ic++) { int Z = (ic == 0) ? tz : zb;
				if (!model->IsSolid(X, Y, Z)) {
					model->SetSolid(X, Y, Z, currentColor);
					voxelCount++;
					any = true;
				}
			}}}
			if (any) { dirty = true; RebuildRenderModel(); }
		}

		void KV6EditorView::DeleteCube() {
			DoPick();
			if (!pickHit)
				return;
			int hx = pickHX, hy = pickHY, hz = pickHZ;
			Vector3 org = model->GetOrigin();
			int xb = mirrorX ? MirrorIdx(hx, -org.x) : hx; int nx = (mirrorX && xb != hx) ? 2 : 1;
			int yb = mirrorY ? MirrorIdx(hy, -org.y) : hy; int ny = (mirrorY && yb != hy) ? 2 : 1;
			int zb = mirrorZ ? MirrorIdx(hz, -org.z) : hz; int nz = (mirrorZ && zb != hz) ? 2 : 1;

			int n = 0;
			for (int ia = 0; ia < nx; ia++) { int X = (ia == 0) ? hx : xb;
			for (int ib = 0; ib < ny; ib++) { int Y = (ib == 0) ? hy : yb;
			for (int ic = 0; ic < nz; ic++) { int Z = (ic == 0) ? hz : zb;
				if (InBounds(X, Y, Z) && model->IsSolid(X, Y, Z))
					n++;
			}}}
			if (n == 0 || voxelCount - n < 1)
				return;
			for (int ia = 0; ia < nx; ia++) { int X = (ia == 0) ? hx : xb;
			for (int ib = 0; ib < ny; ib++) { int Y = (ib == 0) ? hy : yb;
			for (int ic = 0; ic < nz; ic++) { int Z = (ic == 0) ? hz : zb;
				if (InBounds(X, Y, Z) && model->IsSolid(X, Y, Z)) {
					model->SetAir(X, Y, Z);
					voxelCount--;
				}
			}}}
			TrimVolume();
			dirty = true;
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

		// --- Layout / hit testing --------------------------------------------

		void KV6EditorView::LayoutPicker() {
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();
			float bottomClear = 44.0F;

			float gizBox = 96.0F;
			gizR = gizBox * 0.5F - 12.0F;
			gizCx = sw - 16.0F - gizBox * 0.5F;
			gizCy = sh - bottomClear - gizBox * 0.5F;

			presSwatch = (svSize + 6.0F + hueW) / float(presetCols);
			float contentW = svSize + 6.0F + hueW;
			prevH = 22.0F;
			pkW = 8.0F * 2.0F + contentW;
			pkH = 8.0F * 2.0F + svSize + 6.0F + prevH + 6.0F + 2.0F * presSwatch;
			pkX = sw - 16.0F - pkW;
			pkY = (sh - bottomClear - gizBox - 8.0F) - pkH;
			svX = pkX + 8.0F;
			svY = pkY + 8.0F;
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

			mirX0 = 16.0F;
			mirY0 = 116.0F;
			mirBox = 22.0F;
			mirGap = 6.0F;
		}

		bool KV6EditorView::CursorOverPicker(const Vector2& p) const {
			return p.x >= pkX && p.x < pkX + pkW && p.y >= pkY && p.y < pkY + pkH;
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

		bool KV6EditorView::MirrorHitTest(const Vector2& p) {
			for (int i = 0; i < 3; i++) {
				float x = mirX0 + float(i) * (mirBox + mirGap);
				if (InRect(p, x, mirY0, mirBox, mirBox)) {
					if (i == 0) mirrorX = !mirrorX;
					else if (i == 1) mirrorY = !mirrorY;
					else mirrorZ = !mirrorZ;
					return true;
				}
			}
			return false;
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
			for (int k = 0; k < 2; k++) {
				float zz = (k == 0) ? a.z : b.z;
				renderer->AddDebugLine(MakeVector3(a.x, a.y, zz), MakeVector3(b.x, a.y, zz), color);
				renderer->AddDebugLine(MakeVector3(b.x, a.y, zz), MakeVector3(b.x, b.y, zz), color);
				renderer->AddDebugLine(MakeVector3(b.x, b.y, zz), MakeVector3(a.x, b.y, zz), color);
				renderer->AddDebugLine(MakeVector3(a.x, b.y, zz), MakeVector3(a.x, a.y, zz), color);
			}
			renderer->AddDebugLine(MakeVector3(a.x, a.y, a.z), MakeVector3(a.x, a.y, b.z), color);
			renderer->AddDebugLine(MakeVector3(b.x, a.y, a.z), MakeVector3(b.x, a.y, b.z), color);
			renderer->AddDebugLine(MakeVector3(b.x, b.y, a.z), MakeVector3(b.x, b.y, b.z), color);
			renderer->AddDebugLine(MakeVector3(a.x, b.y, a.z), MakeVector3(a.x, b.y, b.z), color);
		}

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
			for (int k = 0; k < 2; k++) {
				float z = (k == 0) ? a.z : b.z;
				renderer->AddDebugLine(MakeVector3(a.x, a.y, z), MakeVector3(b.x, a.y, z), box);
				renderer->AddDebugLine(MakeVector3(b.x, a.y, z), MakeVector3(b.x, b.y, z), box);
				renderer->AddDebugLine(MakeVector3(b.x, b.y, z), MakeVector3(a.x, b.y, z), box);
				renderer->AddDebugLine(MakeVector3(a.x, b.y, z), MakeVector3(a.x, a.y, z), box);
			}
			renderer->AddDebugLine(MakeVector3(a.x, a.y, a.z), MakeVector3(a.x, a.y, b.z), box);
			renderer->AddDebugLine(MakeVector3(b.x, a.y, a.z), MakeVector3(b.x, a.y, b.z), box);
			renderer->AddDebugLine(MakeVector3(b.x, b.y, a.z), MakeVector3(b.x, b.y, b.z), box);
			renderer->AddDebugLine(MakeVector3(a.x, b.y, a.z), MakeVector3(a.x, b.y, b.z), box);
		}

		// Semi-transparent quad at each enabled mirror plane (drawn as a fan of
		// debug lines, since the editor only has line primitives in 3D).
		void KV6EditorView::DrawMirrorPlanes() {
			Vector3 org = model->GetOrigin();
			float lo = -0.5F;
			float hiX = float(model->GetWidth()) - 0.5F;
			float hiY = float(model->GetHeight()) - 0.5F;
			float hiZ = float(model->GetDepth()) - 0.5F;

			if (mirrorX) {
				float px = -org.x;
				Vector4 col = MakeVector4(1.0F, 0.35F, 0.35F, 0.25F);
				for (int i = 0; i <= model->GetHeight(); i += 2)
					renderer->AddDebugLine(MakeVector3(px, float(i) - 0.5F, lo),
					                       MakeVector3(px, float(i) - 0.5F, hiZ), col);
				for (int i = 0; i <= model->GetDepth(); i += 2)
					renderer->AddDebugLine(MakeVector3(px, lo, float(i) - 0.5F),
					                       MakeVector3(px, hiY, float(i) - 0.5F), col);
			}
			if (mirrorY) {
				float py = -org.y;
				Vector4 col = MakeVector4(0.4F, 1.0F, 0.4F, 0.25F);
				for (int i = 0; i <= model->GetWidth(); i += 2)
					renderer->AddDebugLine(MakeVector3(float(i) - 0.5F, py, lo),
					                       MakeVector3(float(i) - 0.5F, py, hiZ), col);
				for (int i = 0; i <= model->GetDepth(); i += 2)
					renderer->AddDebugLine(MakeVector3(lo, py, float(i) - 0.5F),
					                       MakeVector3(hiX, py, float(i) - 0.5F), col);
			}
			if (mirrorZ) {
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
			float pad = 2.0F;
			ColorNP(MakeVector4(0.0F, 0.0F, 0.0F, 0.55F));
			FillRect(pkX, pkY, pkW, pkH);

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

		void KV6EditorView::DrawMirrorToggles() {
			client::IFont& font = fontManager->GetGuiFont();
			font.Draw("Mirror", MakeVector2(mirX0, mirY0 - 15.0F), 0.85F,
			          MakeVector4(0.75F, 0.75F, 0.75F, 1.0F));
			for (int i = 0; i < 3; i++) {
				bool on = (i == 0) ? mirrorX : (i == 1 ? mirrorY : mirrorZ);
				const char* lbl = (i == 0) ? "X" : (i == 1 ? "Y" : "Z");
				float x = mirX0 + float(i) * (mirBox + mirGap);
				ColorNP(on ? MakeVector4(0.22F, 0.50F, 0.28F, 1.0F)
				           : MakeVector4(0.15F, 0.15F, 0.17F, 0.85F));
				FillRect(x, mirY0, mirBox, mirBox);
				StrokeRect(x, mirY0, mirBox, mirBox, 1.0F,
				           on ? MakeVector4(0.5F, 1.0F, 0.6F, 1.0F)
				              : MakeVector4(0.5F, 0.5F, 0.5F, 0.7F));
				font.Draw(lbl, MakeVector2(x + 7.0F, mirY0 + 4.0F), 0.9F,
				          MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
			}
		}

		void KV6EditorView::GizmoAxis(const Vector2& c, Vector3 a, const Vector4& col,
		                              const char* label, client::IFont& font) {
			float rx = Vector3::Dot(a, camRight);
			float ry = Vector3::Dot(a, camUp);
			Vector2 tip = MakeVector2(c.x + rx * gizR, c.y - ry * gizR);
			DrawLine2D(c, tip, 2.5F, col);
			ColorNP(col);
			FillRect(tip.x - 2.5F, tip.y - 2.5F, 5.0F, 5.0F);
			font.Draw(label, MakeVector2(tip.x + (rx >= 0.0F ? 3.0F : -10.0F), tip.y - 6.0F), 0.8F,
			          col);
		}

		void KV6EditorView::DrawGizmo() {
			ColorNP(MakeVector4(0.0F, 0.0F, 0.0F, 0.40F));
			FillRect(gizCx - gizR - 10.0F, gizCy - gizR - 10.0F, (gizR + 10.0F) * 2.0F,
			         (gizR + 10.0F) * 2.0F);
			Vector2 c = MakeVector2(gizCx, gizCy);
			client::IFont& font = fontManager->GetGuiFont();
			GizmoAxis(c, MakeVector3(1, 0, 0), MakeVector4(1.0F, 0.35F, 0.35F, 1.0F), "X", font);
			GizmoAxis(c, MakeVector3(0, 1, 0), MakeVector4(0.40F, 1.0F, 0.40F, 1.0F), "Y", font);
			GizmoAxis(c, MakeVector3(0, 0, 1), MakeVector4(0.45F, 0.60F, 1.0F, 1.0F), "Z", font);
		}

		void KV6EditorView::DrawOverlay(float sw, float sh) {
			client::IFont& font = fontManager->GetGuiFont();
			Vector4 white = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			Vector4 grey = MakeVector4(0.75F, 0.75F, 0.75F, 1.0F);

			font.Draw("KV6 Editor", MakeVector2(16.0F, 12.0F), 1.3F, white);

			std::string name = (!filePath.empty()) ? filePath : "(unsaved model)";
			if (dirty) name += " *";
			font.Draw(name + "   (" + std::to_string(voxelCount) + " voxels)",
			          MakeVector2(16.0F, 40.0F), 1.0F, grey);

			std::string mode = orbitMode ? "Orbit" : "Free-fly";
			font.Draw("Camera: " + mode + "  [Tab] switch  -  [Ctrl+S] save",
			          MakeVector2(16.0F, 60.0F), 1.0F, grey);

			if (statusTimer > 0.0F)
				font.Draw(statusMessage, MakeVector2(16.0F, 80.0F), 1.0F,
				          MakeVector4(0.5F, 1.0F, 0.6F, 1.0F));

			font.Draw("[LMB] place  |  [Alt+LMB] or eyedropper = pick colour  |  [RMB] delete  |  "
			          "hold [MMB] look  |  [WASD]/[Space]/[Ctrl] move  |  [Wheel] zoom  |  [Esc] menu",
			          MakeVector2(16.0F, sh - 28.0F), 1.0F, grey);
		}

		void KV6EditorView::DrawCursor() {
			ColorNP(MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
			if (cursorImg)
				renderer->DrawImage(cursorImg.GetPointerOrNull(),
				                    MakeVector2(cursor.x - 8.0F, cursor.y - 8.0F));
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
			client::IFont& font = fontManager->GetGuiFont();
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
				ColorNP(hover == i ? MakeVector4(0.30F, 0.30F, 0.34F, 1.0F)
				                   : MakeVector4(0.18F, 0.18F, 0.20F, 1.0F));
				FillRect(x, y, w, 36.0F);
				StrokeRect(x, y, w, 36.0F, 1.0F, MakeVector4(0.5F, 0.5F, 0.5F, 0.6F));
				Vector2 ts = font.Measure(kMenuItems[i]);
				font.Draw(kMenuItems[i], MakeVector2(x + (w - ts.x) * 0.5F, y + 8.0F), 1.0F,
				          MakeVector4(1, 1, 1, 1));
				y += 44.0F;
			}
		}

		void KV6EditorView::DrawPrompt(float sw, float sh) {
			client::IFont& font = fontManager->GetGuiFont();
			ColorNP(MakeVector4(0.0F, 0.0F, 0.0F, 0.7F));
			FillRect(0, 0, sw, sh);

			float w = 460.0F, h = 116.0F;
			float x = (sw - w) * 0.5F, y = (sh - h) * 0.5F;
			ColorNP(MakeVector4(0.16F, 0.16F, 0.18F, 1.0F));
			FillRect(x, y, w, h);
			StrokeRect(x, y, w, h, 1.0F, MakeVector4(0.5F, 0.5F, 0.5F, 0.7F));

			font.Draw("Save As (full path)", MakeVector2(x + 16.0F, y + 12.0F), 1.0F,
			          MakeVector4(0.8F, 0.8F, 0.8F, 1.0F));

			float fx = x + 16.0F, fy = y + 44.0F, fw = w - 32.0F, fh = 28.0F;
			ColorNP(MakeVector4(0.05F, 0.05F, 0.06F, 1.0F));
			FillRect(fx, fy, fw, fh);
			StrokeRect(fx, fy, fw, fh, 1.0F, MakeVector4(0.5F, 0.6F, 0.9F, 0.9F));
			std::string shown = promptText + "_";
			font.Draw(shown, MakeVector2(fx + 6.0F, fy + 6.0F), 1.0F, MakeVector4(1, 1, 1, 1));

			font.Draw("[Enter] save    [Esc] cancel", MakeVector2(x + 16.0F, y + h - 24.0F), 0.9F,
			          MakeVector4(0.7F, 0.7F, 0.7F, 1.0F));
		}

		// --- View interface ---------------------------------------------------

		void KV6EditorView::MouseEvent(float dx, float dy) {
			if (lookActive) {
				float sens = 0.003F;
				yaw += dx * sens;
				pitch -= dy * sens;
				float lim = M_PI_F * 0.5F - 0.01F;
				pitch = Clampf(pitch, -lim, lim);
				return;
			}
			cursor.x = Clampf(cursor.x + dx, 0.0F, renderer->ScreenWidth());
			cursor.y = Clampf(cursor.y + dy, 0.0F, renderer->ScreenHeight());
			if (dragPick == 1) UpdateSV(cursor);
			else if (dragPick == 2) UpdateHue(cursor);
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
				else if (key == "Enter") {
					std::string p = promptText;
					if (!p.empty()) {
						if (p.size() < 4 ||
						    !EqualsIgnoringCase(p.substr(p.size() - 4), ".kv6"))
							p += ".kv6";
						filePath = p;
						Save();
					}
					promptOpen = false;
					menuOpen = false;
				} else if (key == "BackSpace") {
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
					else if (b == 2) { promptText = filePath; promptOpen = true; } // Save As
					else if (b == 3) wantsClose = true;      // Exit
				}
				return;
			}

			if (down && key == "Escape") { menuOpen = true; return; }

			if (key == "Control") ctrlHeld = down;
			if (key == "Alt") altHeld = down;
			if (down && ctrlHeld && EqualsIgnoringCase(key, "s")) { Save(); return; }

			if (key == "MiddleMouseButton") { lookActive = down; return; }

			if (key == "LeftMouseButton") {
				if (!down) { dragPick = 0; return; }
				if (PickerMouseDown(cursor)) return;
				if (MirrorHitTest(cursor)) return;
				if (altHeld || pickMode) { Eyedropper(); pickMode = false; return; }
				PlaceCube();
				return;
			}
			if (key == "RightMouseButton" && down) {
				if (CursorOverPicker(cursor)) return;
				DeleteCube();
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

			if (!menuOpen && !promptOpen)
				UpdateMovement(dt);
			if (statusTimer > 0.0F)
				statusTimer -= dt;

			renderer->SetFogColor(MakeVector3(0.10F, 0.10F, 0.12F));
			renderer->SetFogDistance(1000.0F);

			client::SceneDefinition sceneDef = SetupScene(sw, sh);
			camEye = sceneDef.viewOrigin;
			camRight = sceneDef.viewAxis[0];
			camUp = sceneDef.viewAxis[1];
			camFwd = sceneDef.viewAxis[2];
			camFovX = sceneDef.fovX;
			camFovY = sceneDef.fovY;
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
			DrawMirrorPlanes();

			DoPick();
			if (pickHit) {
				DrawCellOutline(pickPX, pickPY, pickPZ, ColorToVec(currentColor));
				DrawCellOutline(pickHX, pickHY, pickHZ, MakeVector4(1.0F, 0.9F, 0.3F, 0.9F));
			}
			renderer->EndScene();

			DrawOverlay(sw, sh);
			LayoutPicker();
			DrawMirrorToggles();
			DrawGizmo();
			DrawPicker();

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
