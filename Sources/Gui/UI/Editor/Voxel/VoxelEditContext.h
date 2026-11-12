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
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Editor/Shell/EditorContext.h>

#include "ReferenceImage.h"

namespace spades {
	namespace gui {
		class ITransformTarget;

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
			// Recolours the selected voxels to `color`, or a waiting paste or
			// import while there is one, which stays waiting. Mirroring does not
			// apply: the selection names every voxel it acts on.
			virtual void RecolorSelection(uint32_t color) = 0;
			// Shows the selection recoloured to `color` without recording it, for
			// a colour still being chosen. Put back at the end of the user action
			// unless committed with RecolorSelection.
			virtual void PreviewRecolorSelection(uint32_t color) = 0;
			// How many voxels the selection commands act on: a waiting paste or
			// import while there is one, else the selected.
			virtual int SelectionCount() const = 0;
			// The solid voxels 6-connected to (x,y,z) through its colour, itself
			// included; empty if (x,y,z) holds no voxel. Changes nothing.
			virtual std::vector<IntVector3> LinkedColorRegion(int x, int y, int z) const = 0;
			/**
			 * The flat face `solid` is part of on its side `normal` (a step along
			 * one axis): it and the voxels reached from it face to face in the
			 * same plane whose face that way shows, as a modeller selects a flat
			 * face. A face a voxel covers is no part of it; nothing when the
			 * face of `solid` itself is covered.
			 */
			virtual std::vector<IntVector3> CoplanarFaces(const IntVector3& solid,
			                                              const IntVector3& normal) const = 0;
			/**
			 * The voxels whose middle lies on the screen where `inside` says
			 * (screen pixels), as the view shows them: those in sight only (one
			 * of their faces seen, not hidden by other voxels), or with
			 * `through` every one there, hidden or not (an X-ray of the volume).
			 */
			virtual std::vector<IntVector3> CellsOnScreen(
			  const std::function<bool(const Vector2&)>& inside, bool through) const = 0;

			// Add every solid voxel of the document to the selection.
			virtual void SelectAll() = 0;
			// Select the solid voxels that are not selected, and no others.
			virtual void InvertSelection() = 0;
			// Add / remove the solid voxels among `cells`.
			virtual void SelectCells(const std::vector<IntVector3>& cells) = 0;
			/**
			 * Makes the tool registered as `id` the active one, once the active
			 * tool is done with its event, as part of the same user action: a
			 * tool hands over to another without being switched from inside
			 * itself.
			 */
			virtual void SwitchTool(const std::string& id) = 0;
			/**
			 * Gives the selection up and switches to Select: what a click on
			 * nothing does in a tool that works on the selection, and can do
			 * nothing without one. Done once the tool's event is over.
			 */
			virtual void ReturnToSelect() = 0;
			virtual void DeselectCells(const std::vector<IntVector3>& cells) = 0;

			// --- The view, and images of it -------------------------------------
			/** Turns the view to look down on the model from the nearest of its
			 *  four corners, `elevation` radians above the horizon, in a
			 *  parallel projection. */
			virtual void LookIsometric(float elevation) = 0;
			/** Turns the view round the model by quarter turns. */
			virtual void TurnView(int quarterTurns) = 0;
			/** Saves an image of the model alone, as the view shows it, on
			 *  `background` (0x00BBGGRR) or with `transparent` on none at all;
			 *  reports where. */
			virtual void SaveRender(std::uint32_t background, bool transparent) = 0;

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
			 *
			 * This is a turn centre, which the user chooses, and not where the
			 * voxels are: use MovedMiddle for that.
			 */
			virtual bool TransformPivot(IntVector3& out) const = 0;
			/**
			 * The middle voxel of what a move moves: the waiting voxels, or else
			 * the selection; false when there is nothing to move. Where the
			 * voxels are, whichever centre turns go round.
			 */
			virtual bool MovedMiddle(IntVector3& out) const = 0;
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
			/**
			 * The voxel nearest the pivot (on a tie the lower one, as for the
			 * middle of a group): the pivot as a cell, which turns go round and
			 * positions in the model's own frame are counted from.
			 */
			virtual IntVector3 PivotVoxel() const = 0;
			// Shows the pivot at `pivot` for a drag in progress, without an undo
			// step. A preview lasts until the user action ends or a command runs:
			// commit it with SetPivot before then, or the pivot goes back.
			virtual void PreviewPivot(const Vector3& pivot) = 0;
			/** The pivot, as the transform panel sets it by number. */
			virtual ITransformTarget& PivotTransform() = 0;

			// --- Sweeping the selection (Extrude, Pocket) ----------------------
			/**
			 * The cells sweeping each selected voxel `steps` voxels along `axis`
			 * (the sign is the direction) would change, mirror images aside: the
			 * empty cells it would fill, or for a pocket (`pocket`) the voxels it
			 * would dig out, the selected ones first. For a preview.
			 */
			virtual std::vector<IntVector3> SweptCells(int axis, int steps, bool pocket) const = 0;
			// --- Cavity --------------------------------------------------------
			/**
			 * Hollows the model out behind the selection, as one undo step: the
			 * selected voxels go, opening a mouth, and the solid they let into
			 * is dug out until only a wall `thickness` voxels thick is left
			 * around it. Nothing else of the model is touched -- the dig never
			 * leaves the solid it is let into -- and the cavity is always open
			 * at the mouth, which a KV6 needs it to be: air sealed inside a
			 * closed shell is not kept by the format and reads back solid.
			 *
			 * Refused, with a status line saying why, unless every selected
			 * voxel is solid and on the model's outside, and unless there is
			 * something behind them to hollow.
			 */
			virtual void CarveCavity(int thickness) = 0;

			/**
			 * Sweeps the selection as one undo step. Extruding fills the cells
			 * each selected voxel passes, in its colour; pocketing digs them out.
			 * The selection then moves to where the sweep ends, so sweeps chain.
			 */
			virtual void SweepSelection(int axis, int steps, bool pocket) = 0;

			// --- Reference images ---------------------------------------------
			// An image across each axis (0 = x, 1 = y, 2 = z) to model from,
			// drawn in the document whatever the tool. A setting of the editor:
			// changing one is no undo step, and none is saved with the document.
			virtual ReferenceImage& Reference(int axis) = 0;
			/** Asks for an image file, then shows it across `axis`, sized to the
			 *  document and set behind it. */
			virtual void LoadReferenceImage(int axis) = 0;
			virtual void ClearReferenceImage(int axis) = 0;
		};
	} // namespace gui
} // namespace spades
