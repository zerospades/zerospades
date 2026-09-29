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
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Editor/Shell/EditorContext.h>

namespace spades {
	namespace gui {
		/**
		 * A whole-voxel change to voxels being moved: `quarterTurns` right-handed
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
		 * The seam a voxel editor's tools operate through: the document's cells,
		 * the selection, the clipboard, mirroring, moving voxels and the pivot,
		 * on top of what every editor offers (IEditorContext). Coordinates are
		 * the document's voxel indices.
		 */
		class IVoxelEditContext : public IEditorContext {
		public:
			// --- Picking (cursor ray vs. the model) ---------------------------
			// Recompute the pick under the cursor; call before reading the rest.
			virtual void DoPick() = 0;
			virtual bool HasPick() const = 0;
			virtual IntVector3 PickPlace() const = 0; // adjacent empty cell
			virtual IntVector3 PickSolid() const = 0; // solid voxel hit

			// --- Cursor ray -----------------------------------------------------
			// Voxel whose centre is nearest where the cursor ray meets the plane
			// (planePoint, normal). Lets tools place points in empty space.
			virtual bool RayPlaneCell(const Vector3& planePoint, const Vector3& normal,
			                          IntVector3& out) = 0;

			// --- Document -----------------------------------------------------
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
			// Removes the selected voxels from the model, or drops a waiting paste
			// or import while there is one (never the last voxel).
			virtual void DeleteSelection() = 0;
			// How many voxels the selection commands act on: a waiting paste or
			// import while there is one, else the selected.
			virtual int SelectionCount() const = 0;
			// The solid voxels 6-connected to (x,y,z) through its colour, itself
			// included; empty if (x,y,z) holds no voxel. Changes nothing.
			virtual std::vector<IntVector3> LinkedColorRegion(int x, int y, int z) const = 0;

			// Add every solid voxel in [lo, hi] to the selection.
			virtual void SelectBox(const IntVector3& lo, const IntVector3& hi) = 0;
			// Add every solid voxel of the document to the selection.
			virtual void SelectAll() = 0;
			// Add / remove the solid voxels among `cells`.
			virtual void SelectCells(const std::vector<IntVector3>& cells) = 0;
			virtual void DeselectCells(const std::vector<IntVector3>& cells) = 0;
			// Apply `cells` with the active tool's action: fill (or erase, if
			// `secondary`) under Draw, select (or deselect) under Select, recolour
			// under Paint (which has no inverse). Lets a sub-tool act correctly in
			// whichever container hosts it.
			virtual void ApplyCells(const std::vector<IntVector3>& cells, bool secondary) = 0;

			// --- Clipboard ----------------------------------------------------
			// Copy and Cut take the selection, or a waiting paste or import while
			// there is one, which they leave where it is rather than place. Each
			// reports what it did, or why not, on the status line.
			virtual void CopySelection() = 0;
			// False when refused (nothing selected, or it would empty the model).
			virtual bool CutSelection() = 0;
			// Starts placing the clipboard's voxels in the Transform tool.
			virtual void Paste() = 0;
			virtual bool CanPaste() const = 0;

			// --- Cell outlines (3D wireframe previews) ---------------------------
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

			// --- Moving voxels ------------------------------------------------
			// Moving or turning the selection writes it straight into the
			// document, one undo step per drag, key or typed value; the moved
			// voxels replace what they land on and stay selected.
			//
			// A paste or an import instead waits (floats) where it would land, so
			// positioning it over other voxels never destroys what it passes. A
			// click away from the gizmo, Enter or leaving the Transform tool
			// places it; Escape or the right button drops it. Every edit of the
			// voxels, the selection or the pivot (and the editor's copy, cut and
			// save) places it first, so it acts on the document as it stands.
			// Pasting, moving, turning, placing and dropping are undo steps.
			virtual bool HasPlacement() const = 0;
			/**
			 * Turns and shifts the waiting voxels, or else the selected voxels in
			 * the document; one undo step. A shift stops at the model size limit.
			 * Turns are about TransformPivot.
			 */
			virtual void TransformPlacement(const PlacementTransform& t) = 0;
			/**
			 * The voxel a transform turns about: the middle of the waiting voxels
			 * (or of the selection), or the model's pivot (see
			 * TurnsAboutModelPivot); false when there is nothing to move. The
			 * middle stays put through the turns of one selection, so four
			 * quarter turns always bring it back where it was.
			 */
			virtual bool TransformPivot(IntVector3& out) const = 0;
			/**
			 * Whether turns go round the model's pivot rather than the middle of
			 * the voxels being turned. Turns keep voxels on voxels only about a
			 * whole voxel, so they go round the one nearest the pivot. An editor
			 * setting: not saved, and not an undo step.
			 */
			virtual bool TurnsAboutModelPivot() const = 0;
			virtual void SetTurnsAboutModelPivot(bool on) = 0;
			/** Writes the waiting voxels into the document as one undo step. */
			virtual void ApplyPlacement() = 0;
			/** Drops the waiting voxels, as one undo step. */
			virtual void CancelPlacement() = 0;
			/** Outlines the waiting voxels (or the selection) as they would land after `t`. */
			virtual void DrawPlacementTransformed(const PlacementTransform& t,
			                                      const Vector4& color) = 0;

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
		};
	} // namespace gui
} // namespace spades
