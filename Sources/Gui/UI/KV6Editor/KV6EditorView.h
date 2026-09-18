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
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "KV6EditorContext.h"
#include "KV6ToolEvent.h"
#include "KV6UndoStack.h"
#include <Gui/UI/Components/EditorMenu.h>
#include <Gui/UI/Components/SoftwareCursor.h>
#include <Gui/View.h>
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
		class EditorUI;
		class KV6ScreenHelper;
		class EditorTool;

		/**
		 * In-app KV6 voxel model editor.
		 *
		 * Hosted by `MainScreen` as a `subview` (like the game `Client`), so the
		 * Runner forwards input and frame events to it. It consumes relative mouse
		 * motion to drive a spectator-style camera and uses a software cursor for
		 * the 2D UI (which may be shared across views).
		 */
		class KV6EditorView : public View, public IEditorContext, public KV6UndoStack::Sink, public IEditorMenuHost {
		public:
			KV6EditorView(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
			              client::FontManager* fontManager, SoftwareCursor* cursor,
			              const std::string& path, bool isNew);

			void MouseEvent(float x, float y) override;
			void WheelEvent(float x, float y) override;
			void KeyEvent(const std::string&, bool down) override;
			void TextInputEvent(const std::string&) override;
			void TextEditingEvent(const std::string&, int start, int len) override;
			bool AcceptsTextInput() override;
			AABB2 GetTextInputRect() override;
			bool NeedsAbsoluteMouseCoordinate() override { return false; }

			void RunFrame(float dt) override;
			void Closing() override {}
			bool WantsToBeClosed() override { return wantsClose; }

			// --- IEditorContext (the seam EditorTool subclasses operate through) ---
			void DoPick() override;
			bool HasPick() const override { return pickHit; }
			IntVector3 PickPlace() const override { return MakeIntVector3(pickPX, pickPY, pickPZ); }
			IntVector3 PickSolid() const override { return MakeIntVector3(pickHX, pickHY, pickHZ); }
			Vector3 ViewDir() const override { return camera.forward; }
			const Vector2& CursorPos() const override { return softwareCursor->GetPosition(); }
			// Voxel whose centre is nearest where the cursor ray meets the plane
			// (planePoint, normal). Lets tools place points in empty space.
			bool RayPlaneCell(const Vector3& planePoint, const Vector3& normal,
			                  IntVector3& out) override;
			// Project a world point to screen pixels. `ok` is false if behind the camera.
			Vector2 WorldToScreen(const Vector3& w, bool& ok) const override;
			void DrawLine3D(const Vector3& a, const Vector3& b, const Vector4& color) override;
			// Selection move (used by the move gizmo).
			bool HasPlacement() const override { return placementActive; }
			bool BeginPlacementFromSelection() override;
			void MovePlacement(int dx, int dy, int dz) override;
			bool PlacementCentroid(Vector3& out) const override;
			void ApplyPlacement() override;
			void CancelPlacement() override;
			void DrawPlacementOffset(int dx, int dy, int dz, const Vector4& color) override;
			void DrawSolidCube(const Vector3& center, float half, const Vector4& color) override;
			GizmoView GetGizmoView() const override;
			void DrawGizmo(const TransformGizmo& gizmo) override;
			bool InBounds(int x, int y, int z) const override;
			VoxelModel& Model() override { return *model; }
			uint32_t CurrentColor() const override { return currentColor; }

			// Selection (a set of solid-voxel coords, shared across tools).
			void ToggleSelect(int x, int y, int z) override;
			void AddSelect(int x, int y, int z) override;
			bool IsSelected(int x, int y, int z) const override;
			void ClearSelection() override;
			int SelectionCount() const override { return int(selection.size()); }
			// Flood-fill: add all 6-connected voxels sharing (x,y,z)'s colour.
			void SelectLinkedColor(int x, int y, int z) override;

			bool PickModeActive() const override { return pickMode; }
			void ClearPickMode() override { pickMode = false; }

			// Pivot (= -origin); moving it keeps the voxels fixed. Undoable.
			Vector3 GetPivot() const override;
			void SetPivot(const Vector3& pivot) override;
			void PreviewPivot(const Vector3& pivot) override;
			void BeginPivotEntry() override;

			// --- Undo / redo (also driven by Ctrl+Z/Y and the toolbar buttons) ---
			// Tools and scripts bracket a multi-step edit so it undoes as one step;
			// nested brackets coalesce. Edits made between Begin/End (incl. plain
			// FillCells/PaintCells/SelectCells calls) are captured automatically.
			void BeginUndoGroup(const std::string& label) override { undo.Begin(label); }
			void EndUndoGroup() override { undo.End(); }
			void Undo() override;
			void Redo() override;
			bool CanUndo() const override { return undo.CanUndo(); }
			bool CanRedo() const override { return undo.CanRedo(); }
			void PlaceCube() override;
			void DeleteCube() override;
			void Eyedropper() override;
			void SetStatus(const std::string&) override;
			void DrawCellOutline(int x, int y, int z, const Vector4& color) override;
			// 3D wireframe over the inclusive voxel range [lo, hi].
			void DrawBoxOutline(const IntVector3& lo, const IntVector3& hi,
			                    const Vector4& color) override;
			bool MirrorEnabled(int axis) const override;
			void SetMirrorEnabled(int axis, bool on) override;
			Vector3 MirrorPlane() const override { return mirrorPlane; }
			void SetMirrorPlane(const Vector3& plane) override;
			void ResetMirrorPlane() override;
			// As above, but also drawing the mirror images for the enabled axes.
			void DrawCellOutlineMirrored(int x, int y, int z, const Vector4& color) override;
			void DrawBoxOutlineMirrored(const IntVector3& lo, const IntVector3& hi,
			                            const Vector4& color) override;
			// Add every solid voxel in [lo, hi] to the selection.
			void SelectBox(const IntVector3& lo, const IntVector3& hi) override;
			// Add the solid voxels among `cells` to the selection.
			void SelectCells(const std::vector<IntVector3>& cells) override;
			// Place a voxel of `color` at each of `cells`, growing the volume to fit.
			void FillCells(const std::vector<IntVector3>& cells, uint32_t color) override;
			// Remove the solid voxels among `cells` (keeps at least one in the model).
			void EraseCells(const std::vector<IntVector3>& cells) override;
			// Recolour the solid voxels among `cells` (no geometry change).
			void PaintCells(const std::vector<IntVector3>& cells, uint32_t color) override;
			// Remove `cells` from the selection.
			void DeselectCells(const std::vector<IntVector3>& cells) override;
			// Fill/erase (Draw) or select/deselect (Select) `cells`, per the active
			// tool's role.
			void ApplyCells(const std::vector<IntVector3>& cells, bool secondary) override;
			Vector4 ColorToVec(uint32_t c) const override;

		protected:
			~KV6EditorView();

		private:
			Handle<client::IRenderer> renderer;
			Handle<client::IAudioDevice> audioDevice;
			Handle<client::FontManager> fontManager;
			Handle<KV6ScreenHelper> io;

			// --- Document -----------------------------------------------------
			Handle<VoxelModel> model;
			Handle<client::IModel> renderModel; // rebuilt on edit
			int cubeSize = 32;
			std::string filePath;
			int voxelCount = 0;
			float globalTime = 0.0F;
			bool wantsClose = false;
			bool wantScreenShot = false; // set by the screenshot key, served at end of frame

			// --- Undo / redo --------------------------------------------------
			// The stack drives the model back and forth through the Sink interface
			// below. The document is dirty while its geometry state differs from the
			// one captured at the last save (-1 = never saved).
			KV6UndoStack undo{*this};
			long savedGeomId = -1;
			bool IsDirty() const { return undo.GeometryStateId() != savedGeomId; }

			// KV6UndoStack::Sink — apply primitives the stack replays on undo/redo.
			void UndoApplyVoxel(int x, int y, int z, bool solid, uint32_t color) override;
			void UndoApplyReframe(int w, int h, int d, int ox, int oy, int oz) override;
			void UndoApplyOrigin(const Vector3& origin) override;
			std::set<int64_t> UndoSnapshotSelection() const override { return selection; }
			void UndoRestoreSelection(const std::set<int64_t>& sel) override { selection = sel; }
			void UndoReplayed() override { RebuildRenderModel(); }

			// The single voxel-write choke point. `WriteVoxel` journals the change for
			// undo; `WriteVoxelRaw` only applies it (used by the stack's replay). Both
			// keep `voxelCount` correct, so no mutator touches it directly.
			void WriteVoxel(int x, int y, int z, bool solid, uint32_t color);
			void WriteVoxelRaw(int x, int y, int z, bool solid, uint32_t color);

			// --- Mode (Blender-style) -----------------------------------------
			// KV6 documents only support Edit mode for now; Object/Animation are
			// shown but disabled.
			enum class EditorMode { Object, Edit, Animation };
			EditorMode currentMode = EditorMode::Edit;

			// --- Tools (available in Edit mode) -------------------------------
			std::vector<std::unique_ptr<EditorTool>> tools;
			int activeTool = 0;
			EditorTool* ActiveTool(); // active tool in Edit mode, else null
			// Switching tools or modes deactivates the outgoing tool, which is where
			// a pending placement is applied.
			void SetActiveTool(int index);
			void SetMode(EditorMode mode);

			// --- Selection ----------------------------------------------------
			std::set<int64_t> selection; // packed voxel keys
			void DrawSelection();
			void ShiftSelection(int ox, int oy, int oz); // keep keys valid on resize

			// --- Clipboard / placement ----------------------------------------
			// Placing voxels (paste, import) is one mechanism: a buffer of voxels
			// follows the cursor until it is dropped. The clipboard is just one
			// source for that buffer, so an import never disturbs a copy.
			struct ClipVoxel {
				IntVector3 rel; // position relative to the buffer's min corner
				uint32_t color;
			};
			std::vector<ClipVoxel> clipboard; // Ctrl+C / Ctrl+X store

			/**
			 * Voxels waiting to be placed (a paste, an import, or a lifted
			 * selection being moved).
			 *
			 * Nothing here has touched the document yet: the voxels are drawn as a
			 * preview and written only when the placement is applied, which is what
			 * keeps a move from destroying whatever it is dragged across. `lifted`
			 * holds the document voxels to clear at that point (empty for a paste or
			 * an import, which take nothing away).
			 */
			struct Placement {
				std::vector<ClipVoxel> voxels; // relative to `anchor`
				IntVector3 anchor;             // min corner, in document coords
				std::vector<IntVector3> lifted;
				std::string label = "Move"; // undo step name
			};
			bool placementActive = false;
			Placement placement;
			// The pending voxels as a renderable model, so they are drawn solid at
			// their temporary position while the document shows the gap they left.
			Handle<client::IModel> placementModel;
			void RebuildPlacementModel();
			// Take the pending voxels out of / put them back into the document
			// without journaling: applying does the journaled edit in one step.
			void LiftPlacementVoxels();
			void RestorePlacementVoxels();

			void CopySelection();
			bool CutSelection(); // returns false if it would empty the document
			void StartPaste();
			// Starts a placement of `voxels` with its min corner at `anchor`, and
			// switches to the Move tool so it can be positioned.
			void StartPlacement(std::vector<ClipVoxel> voxels, const std::string& label,
			                    const IntVector3& anchor);
			void DrawPlacementPreview();
			// Switch to the placement Move sub-tool (where a placement is positioned).
			bool ActivateMoveTool();
			// Loads `path` and starts placing its voxels in the current document.
			void ImportModel(const std::string& path);
			/** Asks for a model with the shared file browser, then imports it. */
			void OpenImportDialog();

			// --- Colour picker (managed by ColorPicker component) ----------------
			uint32_t currentColor = 0xC8C8C8; // packed 0x00BBGGRR
			// What the open picker edits: the shared brush colour when null, else
			// swatch `colorTargetOption` of that tool. Held by identity rather than
			// by option index, since an index shifts as a tool's options change
			// and names a different option in whichever tool is active when the
			// picker reports.
			EditorTool* colorTargetTool = nullptr;
			std::string colorTargetOption;
			bool pickMode = false; // for eyedropper tool (not color picker UI)

			// --- Mirror modelling (reflect each edit across the mirror planes) ---
			// Owned here rather than by a tool, so an edit mirrors whichever tool
			// made it and the planes survive switching tools. The Mirror tool is
			// the UI over this state.
			bool mirrorEnabled[3] = {false, false, false};
			// Plane position per axis, in voxel coordinates. Starts on the pivot.
			// MirrorIdx only sees whole half steps, so SetMirrorPlane keeps it on
			// that grid, and ReframeRaw shifts it along with the voxels.
			Vector3 mirrorPlane = MakeVector3(0.0F, 0.0F, 0.0F);
			bool MirrorOn(int axis) const; // shorthand for MirrorEnabled

			// Orientation gizmo.
			float gizCx, gizCy, gizR;

			// --- Picking ------------------------------------------------------
			bool pickHit = false;
			int pickHX, pickHY, pickHZ; // solid voxel hit
			int pickPX, pickPY, pickPZ; // adjacent empty cell (placement)
			// The camera of the last frame drawn. Picking, projection, panning and
			// the transform gizmo all go through it, so they agree on every pixel.
			GizmoView camera;

			// --- Camera -------------------------------------------------------
			float yaw = -M_PI_F * 0.25F;
			float pitch = -M_PI_F * 0.30F;
			float targetYaw = 0.0F, targetPitch = 0.0F; // navicube animates toward these
			bool camAnim = false;
			Vector3 orbitTarget;
			float orbitDist = 56.0F;
			bool lookActive = false;
			bool keyFwd = false, keyBack = false, keyLeft = false, keyRight = false;
			bool keyUp = false, keyDown = false;
			bool ctrlHeld = false, altHeld = false, shiftHeld = false;
			bool keySprint = false; // cg_keySprint held (tracked like the modifiers)
			// When the descend key went down, for the grace period that keeps a
			// Ctrl chord from moving the camera at all.
			float descendPressTime = 0.0F;
			bool DescendKeyIsActive() const;
			// Distance the Ctrl-bound descend key moved the view during the current
			// Ctrl press; a Ctrl shortcut subtracts it so shortcuts don't move the view.
			Vector3 ctrlDescent = MakeVector3(0.0F, 0.0F, 0.0F);
			bool lmbHeld = false, rmbHeld = false; // for move/drag pointer events

			// Build a typed pointer/key event stamped with the current cursor and
			// modifier state, and route it to the active tool.
			PointerInput MakePointer(PointerButton b, PointerPhase ph,
			                         const Vector2& delta = MakeVector2(0, 0)) const;
			void DispatchPointer(const PointerInput& e);
			// Drop the active tool's gesture in progress before acting behind its back.
			void CancelToolInteraction();

			// --- Cursor / status ----------------------------------------------
			SoftwareCursor* softwareCursor = nullptr;
			std::unique_ptr<SoftwareCursor> ownedCursor; // if no cursor was provided
			std::string statusMessage;
			float statusTimer = 0.0F;

			// --- UI Management -----------------------------------------------
			Handle<EditorUI> ui;
			float screenWidth = 1024.0F; // cache for toolbar hit detection

			// Document
			void NewModel(int n, const std::string& path);
			void LoadModel(const std::string& path);
			int CountSolids();
			void RebuildRenderModel();
			void FrameCamera();
			/** Writes the document to its path; false if there is none, or on error. */
			bool Save();

			// Camera
			Vector3 Forward() const;
			Vector3 CameraEye() const;
			// Far-plane / fog distance, scaled so zooming out never clips the scene.
			float ViewDistance() const;
			// Move the orbit target with cg_keyMove*, cg_keyJump (up) and
			// cg_keyCrouch (down); faster while cg_keySprint is held.
			void UpdateMovement(float dt);
			// Shift + wheel-button drag: slide the view along the screen axes.
			void PanView(float dx, float dy);
			// Forget held movement/look keys whose release a modal may swallow.
			void ReleaseHeldInput();
			client::SceneDefinition SetupScene(float vpX, float vpY, float vpW, float vpH);

			// Editing
			// Index that voxel `i` reflects to across a mirror plane at `plane`.
			int MirrorIdx(int i, float plane) const;
			// Append each cell's mirror images for the enabled axes (Draw and Paint
			// edits).
			void ExpandMirrors(std::vector<IntVector3>& cells) const;
			// Resize/relabel the volume. `ReframeRaw` does the work; `RebuildVolume`
			// also journals it for undo (used by the live mutators).
			void RebuildVolume(int nw, int nh, int nd, int ox, int oy, int oz);
			void ReframeRaw(int nw, int nh, int nd, int ox, int oy, int oz);
			// Set the model origin and rebuild the render model (no journaling).
			void ApplyOriginRaw(const Vector3& origin);
			void TrimVolume();

			// Colour (used by the eyedropper tool)
			uint32_t PackRGB(float r, float g, float b) const;
			uint32_t HSV(float h, float s, float v) const;

			// UI layout + hit testing
			bool InRect(const Vector2& p, float x, float y, float w, float h) const;

			// Editor overlay lines (cell/box outlines, gizmo) are drawn both as
			// depth-tested 3D lines (bright where visible) and collected here to be
			// re-drawn as dim 2D lines on top, so occluded parts still show through.
			struct OverlayLine { Vector3 a, b; Vector4 color; };
			std::vector<OverlayLine> overlayLines;
			void EmitLine(const Vector3& a, const Vector3& b, const Vector4& color);
			void DrawOverlayLines2D();

			// Drawing
			void ColorNP(const Vector4& c);
			void FillRect(float x, float y, float w, float h);
			void StrokeRect(float x, float y, float w, float h, float t, const Vector4& c);
			void DrawLine2D(const Vector2& a, const Vector2& b, float w, const Vector4& col);
			void DrawHelpers();
			void DrawOriginAxes();
			void DrawMirrorPlanes();
			// FreeCAD-style navigation cube (replaces the orientation gizmo): a
			// rotating cube whose faces are clickable to snap the view.
			void DrawNaviCube();
			// Hard-edged filled triangle, for shapes tiled from several triangles.
			void FillTri(const Vector2& a, const Vector2& b, const Vector2& c, const Vector4& col);
			// View direction for the cursor's spot on the cube (face / bevel edge /
			// corner -> ortho / 45deg / isometric). Returns false if not over the cube.
			bool NaviCubeDir(const Vector2& p, Vector3& dir);
			void SnapCameraDir(const Vector3& dir); // animate to look from `dir`
			void DrawOverlay(float sw, float sh);
			void DrawRibbon(float sw); // full-width title/filename bar above the toolbar
			void DrawCursor();

			// Toolbar drawing (delegated to Toolbar/OptionBar components)
			void DrawToolbar(float sw, float sh);
			void DrawSubToolbar(float sw);

			// Total height of ribbon + toolbar + sub-toolbar
			float BarsH();

			// --- IEditorMenuHost (overrides) ---
			std::string GetMenuTitle() override { return "KV6 Editor"; }
			std::vector<EditorMenuItem> GetMenuItems() override;
			bool OnMenuEscape() override;

			// --- Document commands behind the menu items ---
			std::string GetDocumentPath() const { return filePath; }
			std::string GetDocumentExtension() const { return ".kv6"; }
			bool SaveDocument(const std::string& path);
			/** Asks for a path with the shared file browser, then saves to it.
			 *  `after` runs only once the document has actually been written. */
			void OpenSaveAsDialog(std::function<void()> after = std::function<void()>());
			/** Opens another model in place of this one, guarding unsaved changes. */
			void OpenDocument();
			/**
			 * Runs `proceed` once it is safe to lose the current document: right
			 * away when it is clean, otherwise after the user picks Save or Discard.
			 */
			void ConfirmDiscardChanges(std::function<void()> proceed);
		};
	} // namespace gui
} // namespace spades
