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
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "KV6EditorContext.h"
#include "KV6ToolEvent.h"
#include "KV6ToolRegistry.h"
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
			// Pending placement (positioned by the Transform tool).
			bool HasPlacement() const override { return placementActive; }
			void TransformPlacement(const PlacementTransform& t) override;
			bool TransformPivot(IntVector3& out) const override;
			bool TurnsAboutModelPivot() const override { return turnsAboutModelPivot; }
			void SetTurnsAboutModelPivot(bool on) override { turnsAboutModelPivot = on; }
			void ApplyPlacement() override;
			void CancelPlacement() override;
			void DrawPlacementTransformed(const PlacementTransform& t,
			                              const Vector4& color) override;
			void DrawSolidCube(const Vector3& center, float half, const Vector4& color) override;
			GizmoView GetGizmoView() const override;
			void DrawGizmo(const TransformGizmo& gizmo) override;
			bool InBounds(int x, int y, int z) const override;
			VoxelModel& Model() override { return *model; }
			uint32_t CurrentColor() const override { return currentColor; }

			// Selection (a set of solid-voxel coords, shared across tools).
			bool IsSelected(int x, int y, int z) const override;
			void ClearSelection() override;
			void DeleteSelection() override;
			// Voxels lifted by Transform are still what is selected; they only float.
			int SelectionCount() const override {
				return int(selection.size() + (placementActive ? placement.lifted.size() : 0));
			}
			// Flood-fill over the 6-connected voxels sharing (x,y,z)'s colour.
			std::vector<IntVector3> LinkedColorRegion(int x, int y, int z) const override;

			// Clipboard (also driven by Ctrl+C/X/V).
			void CopySelection() override;
			bool CutSelection() override;
			void Paste() override;
			bool CanPaste() const override { return !clipboard.empty(); }

			// Pivot (= -origin); moving it keeps the voxels fixed. Undoable.
			Vector3 GetPivot() const override;
			void SetPivot(const Vector3& pivot) override;
			void PreviewPivot(const Vector3& pivot) override;
			void BeginPivotEntry() override;

			// --- Undo / redo (also driven by Ctrl+Z/Y and the toolbar buttons) ---
			void Undo() override;
			void Redo() override;
			bool CanUndo() const override { return undo.CanUndo(); }
			bool CanRedo() const override { return undo.CanRedo(); }
			void PlaceCube() override;
			void DeleteCube() override;
			void SetStatus(const std::string&) override;
			void DrawCellOutline(int x, int y, int z, const Vector4& color) override;
			// 3D wireframe over the inclusive voxel range [lo, hi].
			void DrawBoxOutline(const IntVector3& lo, const IntVector3& hi,
			                    const Vector4& color) override;
			bool MirrorEnabled(int axis) const override;
			void SetMirrorEnabled(int axis, bool on) override;
			Vector3 MirrorPlane() const override { return mirror.plane; }
			void SetMirrorPlane(const Vector3& plane) override;
			void PreviewMirrorPlane(const Vector3& plane) override;
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
			// What saving would change: the journaled edits, plus voxels still
			// waiting to be placed (a paste leaves the document itself untouched).
			bool HasUnsavedChanges() const { return IsDirty() || placementActive; }

			// KV6UndoStack::Sink — apply primitives the stack replays on undo/redo.
			void UndoApplyVoxel(int x, int y, int z, bool solid, uint32_t color) override;
			void UndoApplyReframe(int w, int h, int d, int ox, int oy, int oz) override;
			void UndoApplyOrigin(const Vector3& origin) override;
			EditState UndoSnapshotState() const override;
			void UndoRestoreState(const EditState& state) override;
			void UndoReplayed() override {
				RebuildRenderModel();
				RebuildPlacementModel();
			}

			// The single voxel-write choke point. `WriteVoxel` journals the change for
			// undo; `WriteVoxelRaw` only applies it (used by the stack's replay). Both
			// keep `voxelCount` correct, so no mutator touches it directly.
			void WriteVoxel(int x, int y, int z, bool solid, uint32_t color);
			void WriteVoxelRaw(int x, int y, int z, bool solid, uint32_t color);

			// --- Mode (Blender-style) -----------------------------------------
			// A .kv6 holds one model, so it is only edited in Edit mode. Object and
			// Animation are shown greyed out: they arrive with .2kv6 scenes.
			enum class EditorMode { Object, Edit, Animation };
			EditorMode currentMode = EditorMode::Edit;
			static bool ModeSupported(EditorMode mode) { return mode == EditorMode::Edit; }

			// --- Tools (available in Edit mode) -------------------------------
			std::vector<ToolSlot> tools;
			int activeTool = 0;
			EditorTool* ActiveTool(); // active tool in Edit mode, else null
			// Switching tools or modes deactivates the outgoing tool, which is where
			// a pending placement is applied.
			void SetActiveTool(int index);
			void SetMode(EditorMode mode);
			// Keys 1-9 pick the active tool's sub-tools, in bar order.
			static std::string SubToolHotKey(int index);
			// Switches tool or sub-tool when `key` is one's hot key; false if not.
			bool SwitchByHotKey(const std::string& key);
			// Whether the editor already handles `key` itself (movement, sprint,
			// delete, screenshot), so it cannot serve as a tool's hot key.
			bool KeyIsTaken(const std::string& key) const;

			// --- Selection ----------------------------------------------------
			std::set<int64_t> selection; // packed voxel keys
			// Adds to the selection without an undo step, for edits that journal
			// the selection they leave as part of their own step.
			void AddSelect(int x, int y, int z);
			void DrawSelection();
			void ShiftSelection(int ox, int oy, int oz); // keep keys valid on resize

			// --- Clipboard / placement ----------------------------------------
			// Placing voxels (paste, import, Transform) is one mechanism: a group of
			// voxels waits, drawn where it would land, until it is placed. The
			// clipboard is just one source for that group, so an import never
			// disturbs a copy.
			std::vector<ClipVoxel> clipboard; // Ctrl+C / Ctrl+X store

			// The pending voxels (see PendingPlacement). Part of the edit state, so
			// lifting, moving, turning, placing and cancelling them are undo steps
			// like any edit.
			bool placementActive = false;
			PendingPlacement placement;
			// Turns go round the model's pivot rather than the voxels' middle.
			bool turnsAboutModelPivot = false;
			// The voxel `group` turns about, per the setting above.
			IntVector3 TurnCentre(const PendingPlacement& group) const;
			// Voxels in the document, counting those lifted by Transform (they only float).
			int DocumentVoxelCount() const {
				return voxelCount + (placementActive ? int(placement.lifted.size()) : 0);
			}
			// A group of `voxels` (relative to `anchor`) turning about their middle;
			// `lifted` names where each came from, if anywhere.
			static PendingPlacement MakePlacement(std::vector<ClipVoxel> voxels,
			                                      const IntVector3& anchor,
			                                      std::vector<IntVector3> lifted,
			                                      const std::string& label);
			// What lifting the selection would take: false when no selected cell
			// holds a voxel. Changes nothing.
			bool PlacementFromSelection(PendingPlacement& out) const;
			// Takes the voxels of `taken` (see PlacementFromSelection) out of the
			// document into the placement, as part of the undo step in progress.
			void LiftIntoPlacement(PendingPlacement taken);
			// The group `t` makes of `from`, kept within the model size limit
			// (`clamped` says whether that held it back); false if it fits nowhere.
			bool TransformedPlacement(const PendingPlacement& from, const PlacementTransform& t,
			                          PendingPlacement& out, bool& clamped) const;
			// Forgets the pending voxels without touching the document or history:
			// for a document being replaced, or once they are placed or put back.
			void DropPlacement();
			// The pending voxels as a renderable model, so they are drawn solid at
			// their temporary position while the document shows the gap they left.
			Handle<client::IModel> placementModel;
			void RebuildPlacementModel();
			// Size of the box holding `voxels` (at least one voxel each way).
			static IntVector3 ExtentOf(const std::vector<ClipVoxel>& voxels);
			// Moves `anchor` to the nearest spot where `voxels` land without the
			// document outgrowing the model size limit; false if none exists.
			bool ClampPlacementAnchor(const std::vector<ClipVoxel>& voxels,
			                          IntVector3& anchor) const;

			/**
			 * Scope of a command that edits the document or the selection, or reads
			 * them as a whole (copy, save). The outermost one applies a pending
			 * placement first, so the command acts on the document as it stands,
			 * and tells the active tool once it is done, so the tool can catch up.
			 * Nested commands (Cut copies) act as one.
			 */
			class DocumentCommand {
			public:
				explicit DocumentCommand(KV6EditorView& editor);
				~DocumentCommand();
				DocumentCommand(const DocumentCommand&) = delete;
				DocumentCommand& operator=(const DocumentCommand&) = delete;

			private:
				KV6EditorView& editor;
			};
			int documentCommandDepth = 0;
			// The document or the selection changed: let the active tool catch up.
			void NotifyDocumentChanged();
			// After undo or redo: pending voxels it brought back are positioned in
			// the Transform tool, and the active tool catches up.
			void HistoryReplayed();

			// Cut and Delete share these: how many selected cells hold a voxel,
			// whether removing them is allowed (saying why not on the status line),
			// and the removal itself as one undo step, returning how many went.
			int SelectedVoxelCount() const;
			bool CanEraseSelection();
			int EraseSelection(const std::string& label);
			// Starts a placement of `voxels` with its min corner at `anchor`, and
			// switches to the Transform tool so it can be positioned.
			void StartPlacement(std::vector<ClipVoxel> voxels, const std::string& label,
			                    const IntVector3& anchor);
			void DrawPlacementPreview();
			// Switch to the placement Transform sub-tool (where a placement is positioned).
			bool ActivateTransformTool();
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
			// Sampling a colour is the editor's, not a tool's: Alt+click, or a
			// click while the picker's eyedropper is armed, samples in any tool.
			bool SamplingArmed() const;
			// Takes the colour of the voxel under the cursor as the brush colour,
			// disarming the eyedropper; false (and still armed) over empty space.
			bool SampleColor();

			// --- Mirror modelling (reflect each edit across the mirror planes) ---
			// Owned here rather than by a tool, so an edit mirrors whichever tool
			// made it and the planes survive switching tools. The Mirror tool is
			// the UI over this state.
			// The planes start on the pivot. MirrorIdx only sees whole half steps,
			// so PlaceMirrorPlane keeps them on that grid, and ReframeRaw shifts
			// them along with the voxels.
			MirrorSetup mirror;
			// Moves the planes without journaling (the setters journal).
			void PlaceMirrorPlane(const Vector3& plane);
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

			// --- User actions -------------------------------------------------
			// A user action is a press to its release, or a key press: its undo
			// steps merge into one, and a preview it shows lasts no longer.
			// Ends the current one: steps stop merging, previews go back.
			void EndUserAction();
			// Ends the user action on leaving the scope when `ends` says the scope
			// completes it (the last release, a key press), even if the tool
			// handling it throws.
			class UserActionEnd {
			public:
				UserActionEnd(KV6EditorView& editor, bool ends) : editor(editor), ends(ends) {}
				~UserActionEnd() {
					if (ends)
						editor.EndUserAction();
				}
				UserActionEnd(const UserActionEnd&) = delete;
				UserActionEnd& operator=(const UserActionEnd&) = delete;

			private:
				KV6EditorView& editor;
				bool ends;
			};
			// Live previews (pivot, mirror planes) remember the value they cover,
			// so committing records from it and ending a preview puts it back.
			std::optional<Vector3> previewedOrigin;
			std::optional<Vector3> previewedMirrorPlane;
			void EndPreviews();

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
			// "Tool › Sub-tool:  hint" for the active tool, or what a click does
			// while sampling a colour.
			std::string ToolHintLine();
			// Under the viewport, bottom up: the editor's keys, the tool hint and
			// the transient status message, each wrapped to the free width.
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
