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
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Components/Gizmo/GizmoView.h>

namespace spades {
	class VoxelModel;
	namespace gui {
		class TransformGizmo;

		/**
		 * A whole-voxel change to pending voxels: `quarterTurns` right-handed
		 * quarter turns about the world axis `axis` (0 = x, 1 = y, 2 = z) through
		 * their pivot, then a shift by `shift`. Right angles and whole voxels
		 * only, so every voxel still lands on a voxel and four turns are exactly
		 * none.
		 */
		struct PlacementTransform {
			IntVector3 shift = IntVector3::Make(0, 0, 0);
			int axis = 2;
			int quarterTurns = 0;

			bool Turns() const { return quarterTurns % 4 != 0; }
			// A turn needs an axis that exists; a plain shift has none to check.
			bool IsValid() const { return !Turns() || (axis >= 0 && axis < 3); }
			bool IsIdentity() const { return !Turns() && shift == IntVector3::Make(0, 0, 0); }
		};

		/**
		 * The editor seam tools operate through.
		 *
		 * Tools never touch `KV6EditorView` directly; they query and mutate the
		 * document, selection, camera and overlay through this narrow interface,
		 * which `KV6EditorView` implements. Keeping it abstract decouples tools from
		 * the host and gives us a single, stable surface to expose to scripts later.
		 */
		class IEditorContext {
		public:
			virtual ~IEditorContext() {}

			// --- Picking (cursor ray vs. the model) ---------------------------
			// Recompute the pick under the cursor; call before reading the rest.
			virtual void DoPick() = 0;
			virtual bool HasPick() const = 0;
			virtual IntVector3 PickPlace() const = 0; // adjacent empty cell
			virtual IntVector3 PickSolid() const = 0; // solid voxel hit

			// --- Camera / cursor ----------------------------------------------
			virtual Vector3 ViewDir() const = 0;
			virtual const Vector2& CursorPos() const = 0;
			// Voxel whose centre is nearest where the cursor ray meets the plane
			// (planePoint, normal). Lets tools place points in empty space.
			virtual bool RayPlaneCell(const Vector3& planePoint, const Vector3& normal,
			                          IntVector3& out) = 0;
			// Project a world point to screen pixels; `ok` is false if behind camera.
			virtual Vector2 WorldToScreen(const Vector3& w, bool& ok) const = 0;

			// --- Document -----------------------------------------------------
			virtual VoxelModel& Model() = 0;
			virtual bool InBounds(int x, int y, int z) const = 0;
			virtual uint32_t CurrentColor() const = 0;
			virtual Vector4 ColorToVec(uint32_t c) const = 0;
			// Place a voxel of `color` at each of `cells`, growing the volume to fit.
			virtual void FillCells(const std::vector<IntVector3>& cells, uint32_t color) = 0;
			// Remove the solid voxels among `cells` (keeps at least one in the model).
			virtual void EraseCells(const std::vector<IntVector3>& cells) = 0;
			// Recolour the solid voxels among `cells` to `color`, without changing the
			// geometry (skips empty cells; never grows the volume).
			virtual void PaintCells(const std::vector<IntVector3>& cells, uint32_t color) = 0;
			// Place / delete at the current pick (Draw's single-voxel ops). Sampling
			// a colour is the editor's own business (Alt+click, the eyedropper), so
			// no tool has to handle it.
			virtual void PlaceCube() = 0;
			virtual void DeleteCube() = 0;

			// --- Selection (a set of solid-voxel coords, shared across tools) ---
			virtual bool IsSelected(int x, int y, int z) const = 0;
			virtual void ClearSelection() = 0;
			// Removes the selected voxels from the model, or drops the pending
			// ones while there are some (never the last voxel).
			virtual void DeleteSelection() = 0;
			// How many voxels the selection commands act on: the pending ones
			// while there are some (lifted, pasted or imported), else the selected.
			virtual int SelectionCount() const = 0;
			// The solid voxels 6-connected to (x,y,z) through its colour, itself
			// included; empty if (x,y,z) holds no voxel. Changes nothing.
			virtual std::vector<IntVector3> LinkedColorRegion(int x, int y, int z) const = 0;

			// Add every solid voxel in [lo, hi] to the selection.
			virtual void SelectBox(const IntVector3& lo, const IntVector3& hi) = 0;
			// Add / remove the solid voxels among `cells`.
			virtual void SelectCells(const std::vector<IntVector3>& cells) = 0;
			virtual void DeselectCells(const std::vector<IntVector3>& cells) = 0;
			// Apply `cells` with the active tool's action: fill (or erase, if
			// `secondary`) under Draw, select (or deselect) under Select, recolour
			// under Paint (which has no inverse). Lets a sub-tool act correctly in
			// whichever container hosts it.
			virtual void ApplyCells(const std::vector<IntVector3>& cells, bool secondary) = 0;

			// --- Clipboard ----------------------------------------------------
			// Copy and Cut take the selection, or the pending voxels while there
			// are some, which they leave where they are rather than place. Each
			// reports what it did, or why not, on the status line.
			virtual void CopySelection() = 0;
			// False when refused (nothing selected, or it would empty the model).
			virtual bool CutSelection() = 0;
			// Starts placing the clipboard's voxels in the Transform tool.
			virtual void Paste() = 0;
			virtual bool CanPaste() const = 0;

			// --- Overlay drawing (3D wireframe previews) ----------------------
			virtual void DrawLine3D(const Vector3& a, const Vector3& b, const Vector4& color) = 0;
			virtual void DrawCellOutline(int x, int y, int z, const Vector4& color) = 0;
			virtual void DrawBoxOutline(const IntVector3& lo, const IntVector3& hi,
			                            const Vector4& color) = 0;
			// --- Mirror modelling ---------------------------------------------
			// Which axes reflect, and the plane each reflects across (in voxel
			// coordinates). Held by the editor rather than by a tool, so an edit
			// mirrors whichever tool made it.
			virtual bool MirrorEnabled(int axis) const = 0;
			virtual void SetMirrorEnabled(int axis, bool on) = 0;
			virtual Vector3 MirrorPlane() const = 0;
			// The axes and planes are journaled: each change below is an undo step.
			/** Moves the planes; each coordinate is quantised to 0.5. */
			virtual void SetMirrorPlane(const Vector3& plane) = 0;
			/**
			 * Shows the planes at `plane` for a drag in progress, without an undo
			 * step. A preview lasts until the user action ends or a command runs:
			 * commit it with SetMirrorPlane before then, or the planes go back.
			 */
			virtual void PreviewMirrorPlane(const Vector3& plane) = 0;
			/** Puts the planes back on the pivot, where they start. */
			virtual void ResetMirrorPlane() = 0;

			// As above, but also drawing the mirror images for the enabled axes.
			virtual void DrawCellOutlineMirrored(int x, int y, int z, const Vector4& color) = 0;
			virtual void DrawBoxOutlineMirrored(const IntVector3& lo, const IntVector3& hi,
			                                    const Vector4& color) = 0;

			// --- Pending placement (floating voxels) --------------------------
			// Paste, import and Transform park their voxels here first: nothing
			// reaches the document until the placement is applied, so dragging
			// voxels over others never destroys what they pass across. A click
			// away from the gizmo, Place, or leaving the Transform tool applies
			// the placement; Cancel or Escape puts it back. Every edit of the
			// voxels, the selection or the pivot (and the editor's copy, cut and
			// save) applies it first, so it acts on the document as it stands.
			// Lifting, moving, turning, applying and cancelling are undo steps.
			virtual bool HasPlacement() const = 0;
			/**
			 * Turns and shifts the pending voxels, lifting the selected voxels out
			 * of the document first when none are pending; one undo step. A shift
			 * stops at the model size limit. Turns are about the pivot, which
			 * moves only with a shift.
			 */
			virtual void TransformPlacement(const PlacementTransform& t) = 0;
			/**
			 * The voxel a transform turns about: the middle of the pending voxels
			 * (or of the selection), or the model's pivot (see
			 * TurnsAboutModelPivot); false when there is nothing to move.
			 */
			virtual bool TransformPivot(IntVector3& out) const = 0;
			/**
			 * Quarter turns made about each world axis since the pending voxels
			 * were lifted, each in 0..3; false when none are pending. A record of
			 * the turns asked for, not a reading of the orientation (see
			 * PendingPlacement::turns).
			 */
			virtual bool TransformTurns(IntVector3& out) const = 0;
			/**
			 * Whether turns go round the model's pivot rather than the middle of
			 * the voxels being turned. Turns keep voxels on voxels only about a
			 * whole voxel, so they go round the one nearest the pivot. An editor
			 * setting: not saved, and not an undo step.
			 */
			virtual bool TurnsAboutModelPivot() const = 0;
			virtual void SetTurnsAboutModelPivot(bool on) = 0;
			/** Writes the pending voxels into the document as one undo step. */
			virtual void ApplyPlacement() = 0;
			/** Puts lifted voxels back where they came from, or drops a paste. */
			virtual void CancelPlacement() = 0;
			/** Outlines the pending voxels (or the selection) as they would land after `t`. */
			virtual void DrawPlacementTransformed(const PlacementTransform& t,
			                                      const Vector4& color) = 0;
			// Opaque, shaded cube of half-size `half` centred at `center`. This is a
			// 2D overlay fill, so call it from a tool's DrawOverlay (not DrawScene).
			virtual void DrawSolidCube(const Vector3& center, float half, const Vector4& color) = 0;

			// --- Transform gizmo ----------------------------------------------
			/** The live 3D view, for a gizmo to pick and drag against. */
			virtual GizmoView GetGizmoView() const = 0;
			/** Draws `gizmo` over the finished scene; call from a tool's DrawOverlay. */
			virtual void DrawGizmo(const TransformGizmo& gizmo) = 0;

			// --- Pivot --------------------------------------------------------
			// The model's pivot point (in editor grid coordinates). Moving it keeps
			// the voxels fixed; the pivot marker follows and it is written to the
			// file on save. The mirror planes start here but do not follow (see
			// ResetMirrorPlane). Float-valued.
			virtual Vector3 GetPivot() const = 0;
			virtual void SetPivot(const Vector3& pivot) = 0;
			// Shows the pivot at `pivot` for a drag in progress, without an undo
			// step. A preview lasts until the user action ends or a command runs:
			// commit it with SetPivot before then, or the pivot goes back.
			virtual void PreviewPivot(const Vector3& pivot) = 0;

			// --- Feedback -----------------------------------------------------
			// A transient message: what an action did, or why it did nothing.
			// What a tool's buttons and keys do belongs in EditorTool::Hint.
			virtual void SetStatus(const std::string&) = 0;

			// --- Undo / redo --------------------------------------------------
			// Edits made through this context are journaled automatically. The
			// editor merges everything done during one user action (a press to its
			// release, a key press) into a single undo step, so a stroke or a
			// multi-step scripted edit undoes at once without the tool grouping it.
			virtual void Undo() = 0;
			virtual void Redo() = 0;
			virtual bool CanUndo() const = 0;
			virtual bool CanRedo() const = 0;
		};
	} // namespace gui
} // namespace spades
