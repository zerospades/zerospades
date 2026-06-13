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

namespace spades {
	class VoxelModel;
	namespace gui {
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
			// Place / delete / sample at the current pick (Draw's single-voxel ops).
			virtual void PlaceCube() = 0;
			virtual void DeleteCube() = 0;
			virtual void Eyedropper() = 0;

			// --- Selection (a set of solid-voxel coords, shared across tools) ---
			virtual void ToggleSelect(int x, int y, int z) = 0;
			virtual void AddSelect(int x, int y, int z) = 0;
			virtual bool IsSelected(int x, int y, int z) const = 0;
			virtual void ClearSelection() = 0;
			virtual int SelectionCount() const = 0;
			// Flood-fill: add all 6-connected voxels sharing (x,y,z)'s colour.
			virtual void SelectLinkedColor(int x, int y, int z) = 0;
			virtual bool SelectionCentroid(Vector3& out) const = 0;
			virtual void MoveSelection(int dx, int dy, int dz) = 0;
			// Add every solid voxel in [lo, hi] to the selection.
			virtual void SelectBox(const IntVector3& lo, const IntVector3& hi) = 0;
			// Add / remove the solid voxels among `cells`.
			virtual void SelectCells(const std::vector<IntVector3>& cells) = 0;
			virtual void DeselectCells(const std::vector<IntVector3>& cells) = 0;
			// Apply `cells` with the active tool's action: fill (or erase, if
			// `secondary`) under Draw, select (or deselect) under Select. Lets a
			// sub-tool act correctly in whichever container hosts it.
			virtual void ApplyCells(const std::vector<IntVector3>& cells, bool secondary) = 0;

			// --- Overlay drawing (3D wireframe previews) ----------------------
			virtual void DrawLine3D(const Vector3& a, const Vector3& b, const Vector4& color) = 0;
			virtual void DrawCellOutline(int x, int y, int z, const Vector4& color) = 0;
			virtual void DrawBoxOutline(const IntVector3& lo, const IntVector3& hi,
			                            const Vector4& color) = 0;
			// As above, but also drawing the mirror images for the enabled axes.
			virtual void DrawCellOutlineMirrored(int x, int y, int z, const Vector4& color) = 0;
			virtual void DrawBoxOutlineMirrored(const IntVector3& lo, const IntVector3& hi,
			                                    const Vector4& color) = 0;
			virtual void DrawSelectionOffset(int dx, int dy, int dz, const Vector4& color) = 0;
			// Opaque, shaded cube of half-size `half` centred at `center` (a solid
			// gizmo handle). This is a 2D overlay fill, so call it from a tool's
			// DrawOverlay (not DrawScene).
			virtual void DrawSolidCube(const Vector3& center, float half, const Vector4& color) = 0;

			// --- Misc editor state / feedback ---------------------------------
			virtual bool PickModeActive() const = 0;
			virtual void ClearPickMode() = 0;
			virtual void SetStatus(const std::string&) = 0;
		};
	} // namespace gui
} // namespace spades
