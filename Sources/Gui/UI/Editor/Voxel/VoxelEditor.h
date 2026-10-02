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
#include <optional>
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Core/RefCountedObject.h>
#include <Gui/UI/Editor/Shell/ToolEvent.h>
#include <Gui/UI/Editor/Shell/ToolOptions.h>
#include <Gui/UI/Editor/Voxel/VoxelEditContext.h>
#include <Gui/UI/Editor/Voxel/VoxelOverlayLines.h>
#include <Gui/UI/Editor/Voxel/VoxelTool.h>
#include <Gui/UI/Editor/Voxel/VoxelUndoStack.h>

namespace spades {
	class VoxelModel;
	namespace client {
		class FontManager;
		class IModel;
		class IRenderer;
	} // namespace client
	namespace gui {
		class EditorUI;
		class IVoxelDocument;
		class IVoxelEditorHost;
		class SoftwareCursor;

		/**
		 * The editing core every voxel editor shares, whatever it edits (a KV6
		 * model, a map) and however it shows it: the tools and the seam they
		 * work through (IVoxelEditContext), the selection, the clipboard, voxels
		 * waiting to be placed, mirroring, the brush colour, undo, the Escape
		 * chain, and the toolbar, option bar, colour picker and hint lines that
		 * drive them.
		 *
		 * It edits an IVoxelDocument and asks its IVoxelEditorHost how the
		 * document is seen and aimed at. The host view owns the camera, renders
		 * the document, and routes its input and drawing through the phases
		 * below, in the order they are listed.
		 */
		class VoxelEditor final : public IVoxelEditContext, public VoxelUndoStack::Sink {
		public:
			struct Config {
				VoxelUndoStack::Limits undo;
				// Registered tool ids to offer, in toolbar order; empty for all.
				std::vector<std::string> tools;
				// Pastes and imports start where the user aims, rather than in the
				// middle of the document (a paste) or on its pivot (an import):
				// for documents too large to see whole.
				bool placeAtAim = false;
			};

			/** `document`, `host`, `ui` and `cursor` must outlive this; the
			 *  editor wires itself to `ui`'s toolbar, option bar and picker. */
			VoxelEditor(IVoxelDocument& document, IVoxelEditorHost& host,
			            client::IRenderer& renderer, client::FontManager& fontManager,
			            EditorUI& ui, SoftwareCursor& cursor, const Config& config);
			~VoxelEditor();
			VoxelEditor(const VoxelEditor&) = delete;
			VoxelEditor& operator=(const VoxelEditor&) = delete;

			// --- Document lifecycle -------------------------------------------
			/** The document now holds something else entirely (new, opened):
			 *  forget the old one's history, selection and pending voxels. */
			void DocumentReplaced();
			/** Names the document's state: equal ids, same voxels. For a host's
			 *  "unsaved changes"; pending voxels are not in it (HasPlacement). */
			long DocumentStateId() const { return undo.DocumentStateId(); }

			/**
			 * Scope of a command that edits the document or the selection, or reads
			 * them as a whole (copy, save). The outermost one ends previews and
			 * applies pending voxels first, so the command acts on the document as
			 * it stands, unless it acts on the pending voxels themselves (`Keep`).
			 * Once done it tells the active tool, so the tool can catch up. Nested
			 * commands (Cut copies) act as one.
			 */
			class DocumentCommand {
			public:
				enum class Pending { Apply, Keep };
				explicit DocumentCommand(VoxelEditor& editor, Pending pending = Pending::Apply);
				// Never throws: a tool failing to catch up is logged, not rethrown.
				~DocumentCommand();
				DocumentCommand(const DocumentCommand&) = delete;
				DocumentCommand& operator=(const DocumentCommand&) = delete;

			private:
				VoxelEditor& editor;
			};

			/** Starts placing `model`'s voxels (an import), its pivot on the
			 *  document's, in the Transform tool. */
			void InsertModel(const VoxelModel& model);

			void Undo();
			void Redo();

			// --- Tools, for a host offering them elsewhere (a pie menu) ---------
			int ToolCount() const { return int(tools.size()); }
			/** The label of tool `index`, as on its toolbar button. */
			std::string ToolLabel(int index) const;
			/** Makes tool `index` the active one, as its toolbar button does. */
			void SelectTool(int index);

			// --- Input, in the host's routing order ---------------------------
			/** Every key, even while a modal has the keyboard, so no modifier
			 *  stays "held" after a release it swallowed. */
			void TrackModifiers(const std::string& key, bool down);
			/** Whether Ctrl+`key` is one of the editor's shortcuts. */
			static bool IsEditShortcut(const std::string& key);
			/** Runs Ctrl+`key`, a shortcut per IsEditShortcut. */
			void RunEditShortcut(const std::string& key);
			/** The cursor moved by `delta`: the picker and the active tool follow. */
			void PointerMoved(const Vector2& delta);
			/** A press or release of the left or right button. */
			void ButtonEvent(PointerButton button, bool down);
			/** A key typed into a number box of the option bar; true if taken. */
			bool OptionBarKey(const std::string& key);
			/** The editor's own keys (delete); true if taken. Before the host's
			 *  movement keys, which never reach the tools. */
			bool CommandKey(const std::string& key, bool down);
			/** Tool and sub-tool hot keys, then the active tool's keys. */
			void ToolKey(const std::string& key, bool down);
			/** A modal took the input: held buttons and keys are let go. */
			void ReleaseHeldInput();
			/** Abandons the active tool's gesture, before a command acts behind
			 *  its back (a host shortcut, a dialog taking the input). */
			void CancelToolInteraction();
			bool IsCtrlHeld() const { return ctrlHeld; }
			bool IsShiftHeld() const { return shiftHeld; }
			/** Backs out of the top of the Escape chain; false with nothing to
			 *  back out of (the host then opens its menu). */
			bool EscapeOnce();

			// --- Frame, in the host's drawing order ---------------------------
			void Update(float dt);
			/** The pending voxels, selection, mirror planes and the active tool's
			 *  preview; inside the host's scene. */
			void DrawScene();
			/** Over the finished scene: the overlay lines, then the hint lines
			 *  under the viewport, `help` (the host's own keys) at the bottom. */
			void DrawViewportOverlays(float sw, float sh, const std::string& help);
			/** The toolbar and the option bar under the host's ribbon. */
			void DrawBars(float sw);
			/** The colour picker and the active tool's overlay, over everything
			 *  but the host's menu and dialogs. */
			void DrawPanels();

			// --- Layout: the bars the editor draws across the top ------------
			static float RibbonHeight();
			/** Ribbon, toolbar and option bar: the viewport starts below. */
			static float BarsHeight();

			// --- IEditorContext -----------------------------------------------
			Vector3 ViewDir() const override;
			const Vector2& CursorPos() const override;
			void DrawLine3D(const Vector3& a, const Vector3& b, const Vector4& color) override;
			GizmoView GetGizmoView() const override;
			void DrawGizmo(const TransformGizmo& gizmo) override;
			void SetStatus(const std::string&) override;

			// --- IVoxelEditContext --------------------------------------------
			void DoPick() override;
			bool HasPick() const override { return pickHit; }
			IntVector3 PickPlace() const override { return pickPlace; }
			IntVector3 PickSolid() const override { return pickSolid; }
			bool RayPlaneCell(const Vector3& planePoint, const Vector3& normal,
			                  IntVector3& out) override;
			std::uint32_t CurrentColor() const override { return currentColor; }
			Vector4 ColorToVec(std::uint32_t c) const override;
			void FillCells(const std::vector<IntVector3>& cells, std::uint32_t color) override;
			void EraseCells(const std::vector<IntVector3>& cells) override;
			void PaintCells(const std::vector<IntVector3>& cells, std::uint32_t color) override;
			void PlaceCube() override;
			void DeleteCube() override;
			bool IsSelected(int x, int y, int z) const override;
			void ClearSelection() override;
			void DeleteSelection() override;
			void RecolorSelection(std::uint32_t color) override;
			int SelectionCount() const override;
			std::vector<IntVector3> LinkedColorRegion(int x, int y, int z) const override;
			void SelectAll() override;
			void SelectCells(const std::vector<IntVector3>& cells) override;
			void DeselectCells(const std::vector<IntVector3>& cells) override;
			void CopySelection() override;
			bool CutSelection() override;
			void Paste() override;
			bool CanPaste() const override { return !clipboard.empty(); }
			void DrawCellOutline(int x, int y, int z, const Vector4& color) override;
			void DrawBoxOutline(const IntVector3& lo, const IntVector3& hi,
			                    const Vector4& color) override;
			bool MirrorEnabled(int axis) const override;
			void SetMirrorEnabled(int axis, bool on) override;
			Vector3 MirrorPlane() const override { return edit.mirror.plane; }
			void SetMirrorPlane(const Vector3& plane) override;
			void PreviewMirrorPlane(const Vector3& plane) override;
			void ResetMirrorPlane() override;
			void DrawCellOutlineMirrored(int x, int y, int z, const Vector4& color) override;
			void DrawBoxOutlineMirrored(const IntVector3& lo, const IntVector3& hi,
			                            const Vector4& color) override;
			bool HasPlacement() const override { return edit.placing; }
			void TransformPlacement(const PlacementTransform& t) override;
			bool TransformPivot(IntVector3& out) const override;
			bool TurnsAboutModelPivot() const override { return turnsAboutModelPivot; }
			void SetTurnsAboutModelPivot(bool on) override { turnsAboutModelPivot = on; }
			void ApplyPlacement() override;
			void CancelPlacement() override;
			void DrawPlacementTransformed(const PlacementTransform& t,
			                              const Vector4& color) override;
			Vector3 GetPivot() const override;
			void SetPivot(const Vector3& pivot) override;
			void PreviewPivot(const Vector3& pivot) override;

		private:
			IVoxelDocument& document;
			IVoxelEditorHost& host;
			Handle<client::IRenderer> renderer;
			Handle<client::FontManager> fontManager;
			EditorUI& ui;
			SoftwareCursor& cursor;

			// --- Undo / redo --------------------------------------------------
			VoxelUndoStack undo;

			// VoxelUndoStack::Sink — apply primitives the stack replays on undo/redo.
			void UndoApplyVoxel(int x, int y, int z, bool solid, std::uint32_t color) override;
			void UndoApplyReframe(int w, int h, int d, int ox, int oy, int oz) override;
			void UndoApplyOrigin(const Vector3& origin) override;
			EditState UndoSnapshotState() const override { return edit; }
			void UndoRestoreState(const EditState& state) override { edit = state; }
			void UndoReplayed() override {}

			bool InBounds(int x, int y, int z) const;
			// Inside the volume and editable: the only voxels edits touch.
			bool Editable(int x, int y, int z) const;
			// The one place voxels are written: `WriteVoxel` journals the change
			// for undo and drops a removed voxel from the selection, so the
			// selection only ever holds solid voxels.
			void WriteVoxel(int x, int y, int z, bool solid, std::uint32_t color);

			// --- Mode (Blender-style) -----------------------------------------
			// Voxels are only edited in Edit mode. Object and Animation are shown
			// greyed out: they arrive with .2kv6 scenes.
			enum class EditorMode { Object, Edit, Animation };
			EditorMode currentMode = EditorMode::Edit;
			static bool ModeSupported(EditorMode mode) { return mode == EditorMode::Edit; }
			struct ModeInfo {
				EditorMode mode;
				const char* id;
				const char* label;
			};
			static const std::vector<ModeInfo>& Modes();

			// --- Tools --------------------------------------------------------
			std::vector<VoxelToolSlot> tools;
			int activeTool = 0;
			VoxelTool* ActiveTool(); // active tool in Edit mode, else null
			int ToolIndex(const std::string& id) const;
			// The active tool's option `id` if it is of `type`, else null: where a
			// click on the option bar lands.
			ToolOption* ActiveOption(const std::string& id, ToolOption::Type type);
			void SetActiveTool(int index);
			void SetMode(EditorMode mode);
			// Deactivates the outgoing tool (where a pending placement lands) and
			// activates tool `toolIndex` in `mode`.
			void Activate(int toolIndex, EditorMode mode);
			// Keys 1-9 pick the active tool's sub-tools, in bar order.
			static std::string SubToolHotKey(int index);
			bool SwitchByHotKey(const std::string& key);
			// Whether a key already does something (the host's keys, delete), so
			// it cannot serve as a tool's hot key.
			bool KeyIsTaken(const std::string& key) const;
			void WireUI();

			// --- Edit state ---------------------------------------------------
			// Everything besides the voxels that undo restores: the selection, the
			// mirror setup and the pending voxels.
			EditState edit;
			void SelectIfSolid(const IntVector3& v);
			void SelectBox(const IntVector3& lo, const IntVector3& hi);

			// --- Clipboard / placement ----------------------------------------
			const bool placeAtAim;
			std::vector<ClipVoxel> clipboard; // Ctrl+C / Ctrl+X store
			// Where voxels spanning `extent` start when placed at the aim: sitting
			// on the voxel aimed at, centred across it. False when nothing is.
			bool AimedAnchor(const IntVector3& extent, IntVector3& anchor);
			bool turnsAboutModelPivot = false;
			IntVector3 TurnCentre(const IntVector3& groupMiddle) const;
			static PendingPlacement MakePlacement(std::vector<ClipVoxel> voxels,
			                                      const IntVector3& anchor,
			                                      const std::string& label);
			bool PlacementFromSelection(PendingPlacement& out) const;
			// Moves the selected voxels by `t` in the document, as one undo step:
			// they leave where they were, replace what they land on, and stay
			// selected.
			void MoveSelection(const PlacementTransform& t);
			// Writes `group` into the document within the step open; what it
			// writes becomes the selection. Returns how many voxels it wrote.
			int LandGroup(const PendingPlacement& group);
			bool LandVoxel(const IntVector3& at, std::uint32_t color);
			// The group `t` makes of `from`, kept within the size limit (`clamped`
			// says whether that held it back); false if it fits nowhere.
			bool TransformedPlacement(const PendingPlacement& from, const PlacementTransform& t,
			                          PendingPlacement& out, bool& clamped) const;
			// The middle the selection turns about, kept with the voxels a move
			// moved, so four quarter turns of one selection bring it back.
			struct SelectionMiddle {
				std::uint64_t selectionVersion = 0; // the selection it belongs to
				bool valid = false;
				IntVector3 at = IntVector3::Make(0, 0, 0);
			} movedMiddle;
			bool SelectionMiddleOf(IntVector3& out) const;
			void DropPlacement();
			// The pending voxels as a renderable model, rebuilt in Update when they
			// changed, and their grid, which the overlay lines test against.
			Handle<client::IModel> placementModel;
			Handle<VoxelModel> placementVoxels;
			std::uint64_t placementModelVersion = ~std::uint64_t(0);
			// Moves `anchor` to the nearest spot where a box of `extent` lands
			// within the size limit; false if none exists.
			bool ClampPlacementAnchor(const IntVector3& extent, IntVector3& anchor) const;
			bool CanEraseSelection();
			int EraseSelection(const std::string& label);
			void StartPlacement(std::vector<ClipVoxel> voxels, const std::string& label,
			                    const IntVector3& anchor);
			bool ActivateTransformTool();

			int documentCommandDepth = 0;
			void NotifyDocumentChanged();
			void HistoryReplayed();

			// --- Volume -------------------------------------------------------
			// A volume to reframe to: its size, and the shift moving today's voxels
			// into it.
			struct VolumeFrame {
				IntVector3 size;
				IntVector3 shift;
			};
			bool Fits(const VolumeFrame& frame) const;
			// The smallest volume holding the document and the box [lo, hi].
			VolumeFrame FrameHolding(const IntVector3& lo, const IntVector3& hi) const;
			// Moves the volume to `frame`, journaled, unless it is already there.
			void Reframe(const VolumeFrame& frame);
			void RebuildVolume(const IntVector3& size, const IntVector3& shift);
			void ReframeRaw(const IntVector3& size, const IntVector3& shift);
			void TrimVolume();
			std::string SizeLimitMessage() const;

			// --- Editing ------------------------------------------------------
			int MirrorIdx(int i, float plane) const;
			void ExpandMirrors(std::vector<IntVector3>& cells) const;
			// Every edit of cells goes through these two, mirror images included,
			// as one undo step `label`.
			void Fill(std::vector<IntVector3> cells, std::uint32_t color, const std::string& label);
			void Erase(std::vector<IntVector3> cells, const std::string& label);

			// --- Colour -------------------------------------------------------
			// The brush colour, shared by every tool; a setting of the editor, not
			// of the document, so choosing one is never an undo step.
			std::uint32_t currentColor = 0xC8C8C8; // packed 0x00BBGGRR
			void NoteColorUsed(std::uint32_t color);
			// Alt+click, or a click while the eyedropper is armed, samples in any tool.
			bool SamplingArmed() const;
			bool SampleColor();

			// --- Mirror -------------------------------------------------------
			void PlaceMirrorPlane(const Vector3& plane);
			void DrawMirrorPlanes();

			// --- Picking ------------------------------------------------------
			// The host's view in document coordinates (see DocumentOrigin):
			// what picking, projecting and the gizmos go through.
			GizmoView DocumentView() const;
			bool pickHit = false;
			IntVector3 pickSolid = IntVector3::Make(0, 0, 0); // solid voxel hit
			IntVector3 pickPlace = IntVector3::Make(0, 0, 0); // adjacent empty cell

			// --- Input --------------------------------------------------------
			bool ctrlHeld = false, altHeld = false, shiftHeld = false;
			bool lmbHeld = false, rmbHeld = false;
			PointerInput MakePointer(PointerButton b, PointerPhase ph,
			                         const Vector2& delta = MakeVector2(0, 0)) const;
			void DispatchPointer(const PointerInput& e);

			// --- User actions -------------------------------------------------
			// A user action is a press to its release, or a key press: its undo
			// steps merge into one, and a preview it shows lasts no longer.
			void EndUserAction() noexcept;
			class UserActionEnd {
			public:
				UserActionEnd(VoxelEditor& editor, bool ends) : editor(editor), ends(ends) {}
				~UserActionEnd() {
					if (ends)
						editor.EndUserAction();
				}
				UserActionEnd(const UserActionEnd&) = delete;
				UserActionEnd& operator=(const UserActionEnd&) = delete;

			private:
				VoxelEditor& editor;
				bool ends;
			};
			std::optional<Vector3> previewedOrigin;
			std::optional<Vector3> previewedMirrorPlane;
			void EndPreviews() noexcept;

			// --- Escape -------------------------------------------------------
			// What Escape backs out of, top-most first. Escape does it and the
			// hint line names it, both from here, so the line says what it does.
			enum class EscapeLayer { None, Eyedropper, Tool, Placement, Selection };
			EscapeLayer NextEscape();
			std::string EscapeLabel(EscapeLayer layer);
			void Escape(EscapeLayer layer);

			// --- Drawing ------------------------------------------------------
			VoxelOverlayLines overlayLines;
			std::string statusMessage;
			float statusTimer = 0.0F;
			float screenWidth = 1024.0F; // for toolbar hit tests
			void RefreshPlacementModel();
			void DrawSelection();
			void DrawPlacementPreview();
			std::string ToolHintLine();
			void DrawToolbar(float sw);
			void DrawOptionBar(float sw);
		};
	} // namespace gui
} // namespace spades
