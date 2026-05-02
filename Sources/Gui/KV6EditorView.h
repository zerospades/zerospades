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

#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "View.h"
#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Client/SceneDefinition.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	class VoxelModel;
	namespace client {
		class FontManager;
		class IModel;
		class IFont;
		class IImage;
	} // namespace client
	namespace gui {
		class KV6ScreenHelper;
		class EditorTool;

		/**
		 * In-app KV6 voxel model editor.
		 *
		 * Hosted by `MainScreen` as a `subview` (like the game `Client`), so the
		 * Runner forwards input and frame events to it. It consumes relative mouse
		 * motion to drive a spectator-style camera and tracks its own software
		 * cursor for the 2D UI.
		 */
		class KV6EditorView : public View {
		public:
			KV6EditorView(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
			              client::FontManager* fontManager, const std::string& path, bool isNew);

			void MouseEvent(float x, float y) override;
			void WheelEvent(float x, float y) override;
			void KeyEvent(const std::string&, bool down) override;
			void TextInputEvent(const std::string&) override;
			bool AcceptsTextInput() override;
			AABB2 GetTextInputRect() override;
			bool NeedsAbsoluteMouseCoordinate() override { return false; }

			void RunFrame(float dt) override;
			void RunFrameLate(float dt) override;
			void Closing() override {}
			bool WantsToBeClosed() override { return wantsClose; }

			// --- Tool-facing API (used by EditorTool subclasses) --------------
			void DoPick();
			bool HasPick() const { return pickHit; }
			IntVector3 PickPlace() const { return MakeIntVector3(pickPX, pickPY, pickPZ); }
			IntVector3 PickSolid() const { return MakeIntVector3(pickHX, pickHY, pickHZ); }
			bool InBounds(int x, int y, int z) const;
			VoxelModel& Model() { return *model; }
			uint32_t CurrentColor() const { return currentColor; }

			// Selection (a set of solid-voxel coords, shared across tools).
			void ToggleSelect(int x, int y, int z);
			void AddSelect(int x, int y, int z);
			bool IsSelected(int x, int y, int z) const;
			void ClearSelection();
			int SelectionCount() const { return int(selection.size()); }
			// Flood-fill: add all 6-connected voxels sharing (x,y,z)'s colour.
			void SelectLinkedColor(int x, int y, int z);

			bool AltHeld() const { return altHeld; }
			bool PickModeActive() const { return pickMode; }
			void ClearPickMode() { pickMode = false; }
			void PlaceCube();
			void DeleteCube();
			void Eyedropper();
			void SetStatus(const std::string&);
			void DrawCellOutline(int x, int y, int z, const Vector4& color);
			// 3D wireframe over the inclusive voxel range [lo, hi].
			void DrawBoxOutline(const IntVector3& lo, const IntVector3& hi, const Vector4& color);
			// Add every solid voxel in [lo, hi] to the selection.
			void SelectBox(const IntVector3& lo, const IntVector3& hi);
			// Add the solid voxels among `cells` to the selection.
			void SelectCells(const std::vector<IntVector3>& cells);
			// Place a voxel of `color` at each of `cells`, growing the volume to fit.
			void FillCells(const std::vector<IntVector3>& cells, uint32_t color);
			Vector4 ColorToVec(uint32_t c) const;

		protected:
			~KV6EditorView();

		private:
			Handle<client::IRenderer> renderer;
			Handle<client::IAudioDevice> audioDevice;
			Handle<client::FontManager> fontManager;
			Handle<KV6ScreenHelper> io;
			Handle<client::IImage> cursorImg;

			// --- Document -----------------------------------------------------
			Handle<VoxelModel> model;
			Handle<client::IModel> renderModel; // rebuilt on edit
			int cubeSize = 32;
			std::string filePath;
			bool dirty = false;
			int voxelCount = 0;
			float globalTime = 0.0F;
			bool wantsClose = false;

			// --- Mode (Blender-style) -----------------------------------------
			// KV6 documents only support Edit mode for now; Object/Animation are
			// shown but disabled.
			enum class EditorMode { Object, Edit, Animation };
			EditorMode currentMode = EditorMode::Edit;

			// --- Tools (available in Edit mode) -------------------------------
			std::vector<std::unique_ptr<EditorTool>> tools;
			int activeTool = 0;
			EditorTool* ActiveTool(); // active tool in Edit mode, else null

			// --- Selection ----------------------------------------------------
			std::set<int64_t> selection; // packed voxel keys
			void DrawSelection();
			void ShiftSelection(int ox, int oy, int oz); // keep keys valid on resize

			// --- Colour picker (HSV is the source of truth) -------------------
			uint32_t currentColor = 0xC8C8C8; // packed 0x00BBGGRR
			float hue = 0.0F, sat = 0.0F, val = 0.78F;
			std::vector<uint32_t> presets;
			int presetCols = 8;
			int dragPick = 0; // 0 none, 1 SV square, 2 hue bar
			bool pickMode = false;

			// Picker panel geometry (recomputed each frame).
			float pkX, pkY, pkW, pkH;
			float svX, svY, svSize = 150.0F;
			float hueX, hueY, hueW = 16.0F;
			float prevX, prevY, prevW, prevH;
			float eyeX, eyeY, eyeS;
			float presX, presY, presSwatch;

			// --- Mirror modelling (reflect each edit across the pivot plane) ---
			bool mirrorX = false, mirrorY = false, mirrorZ = false;
			float mirX0, mirY0, mirBox, mirGap;

			// Orientation gizmo.
			float gizCx, gizCy, gizR;

			// --- Picking ------------------------------------------------------
			bool pickHit = false;
			int pickHX, pickHY, pickHZ; // solid voxel hit
			int pickPX, pickPY, pickPZ; // adjacent empty cell (placement)
			Vector3 camEye, camRight, camUp, camFwd;
			float camFovX, camFovY, camSW, camSH; // camSW/SH = viewport size
			float camVpX, camVpY;                 // viewport top-left (below the bars)

			// --- Camera -------------------------------------------------------
			float yaw = -M_PI_F * 0.25F;
			float pitch = -M_PI_F * 0.30F;
			bool orbitMode = true;
			Vector3 freePos;
			Vector3 orbitTarget;
			float orbitDist = 56.0F;
			bool lookActive = false;
			bool keyFwd = false, keyBack = false, keyLeft = false, keyRight = false;
			bool keyUp = false, keyDown = false;
			bool ctrlHeld = false, altHeld = false;

			// --- Cursor / status ----------------------------------------------
			Vector2 cursor;
			std::string statusMessage;
			float statusTimer = 0.0F;

			// --- Pause menu (Esc) + Save As prompt ----------------------------
			bool menuOpen = false;
			bool promptOpen = false;
			std::string promptText;

			// Document
			void NewModel(int n, const std::string& path);
			void LoadModel(const std::string& path);
			int CountSolids();
			void RebuildRenderModel();
			void FrameCamera();
			void Save();

			// Camera
			Vector3 Forward() const;
			Vector3 CameraEye() const;
			void ToggleCameraMode();
			void UpdateMovement(float dt);
			client::SceneDefinition SetupScene(float vpX, float vpY, float vpW, float vpH);

			// Editing
			int MirrorIdx(int i, float pivot) const;
			void RebuildVolume(int nw, int nh, int nd, int ox, int oy, int oz);
			void TrimVolume();

			// Colour
			uint32_t PackRGB(float r, float g, float b) const;
			uint32_t HSV(float h, float s, float v) const;
			void BuildPresets();
			void RGBToHSV(uint32_t c);
			void SyncColor();

			// UI layout + hit testing
			void LayoutPicker();
			bool CursorOverPicker(const Vector2& p) const;
			bool InRect(const Vector2& p, float x, float y, float w, float h) const;
			void UpdateSV(const Vector2& p);
			void UpdateHue(const Vector2& p);
			bool PickerMouseDown(const Vector2& p);
			bool MirrorHitTest(const Vector2& p);

			// Drawing
			void ColorNP(const Vector4& c);
			void FillRect(float x, float y, float w, float h);
			void StrokeRect(float x, float y, float w, float h, float t, const Vector4& c);
			void DrawLine2D(const Vector2& a, const Vector2& b, float w, const Vector4& col);
			void DrawHelpers();
			void DrawOriginAxes();
			void DrawMirrorPlanes();
			void DrawPicker();
			void DrawMirrorToggles();
			void DrawGizmo();
			void GizmoAxis(const Vector2& c, Vector3 a, const Vector4& col, const char* label,
			               client::IFont& font);
			void DrawOverlay(float sw, float sh);
			void DrawRibbon(float sw); // full-width title/filename bar above the toolbar
			void DrawCursor();

			// Unified top toolbar: modes on the left, a separator, then the tools
			// available in the current mode.
			struct ToolbarHit {
				enum Kind { None, Mode, Tool } kind = None;
				int index = -1;
			};
			ToolbarHit ToolbarHitTest(const Vector2& p);
			void DrawToolbar(float sw, float sh);

			// Secondary toolbar showing the active tool's sub-tools (always shown).
			float BarsH(); // total height of ribbon + toolbar + sub-toolbar
			int SubToolbarHitTest(const Vector2& p); // -1 none, else sub-tool index
			void DrawSubToolbar(float sw);

			// Pause menu / Save As prompt
			int MenuButtonAt(const Vector2& p); // -1 none, else index
			void DrawMenu(float sw, float sh);
			void DrawPrompt(float sw, float sh);
		};
	} // namespace gui
} // namespace spades
