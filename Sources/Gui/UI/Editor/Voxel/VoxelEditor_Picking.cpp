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
// VoxelEditor: what the cursor aims at, and the volume the model is held in.

#include "VoxelEditorInternal.h"

namespace spades {
	namespace gui {
		// --- Picking ----------------------------------------------------------

		Vector3 VoxelEditor::ViewDir() const { return host.CurrentView().forward; }

		GizmoView VoxelEditor::DocumentView() const {
			// The same view, with the world shifted so voxel i is centred at i.
			GizmoView view = host.CurrentView();
			view.eye = view.eye - host.DocumentOrigin();
			return view;
		}
		const Vector2& VoxelEditor::CursorPos() const { return host.AimPosition(); }

		bool VoxelEditor::RayPlaneCell(const Vector3& planePoint, const Vector3& normal,
		                               IntVector3& out) {
			const GizmoView view = DocumentView();
			if (!view.IsValid())
				return false;
			Vector3 origin, dir;
			view.Ray(host.AimPosition(), origin, dir);
			float denom = Vector3::Dot(dir, normal);
			if (std::fabs(denom) < 1.0e-5F)
				return false;
			float t = Vector3::Dot(planePoint - origin, normal) / denom;
			if (t <= 0.0F)
				return false;
			Vector3 hit = origin + dir * t;
			// A ray grazing the plane meets it arbitrarily far away; no voxel lies
			// there, and the cell would not even fit in an int. No cell the editor
			// works with lies farther out than a few times the largest document.
			const IntVector3 maxSize = document.MaxSize();
			const float reach =
			  float(std::max(maxSize.x, std::max(maxSize.y, maxSize.z))) * 4.0F;
			for (float c : {hit.x, hit.y, hit.z}) {
				if (!std::isfinite(c) || std::fabs(c) > reach)
					return false;
			}
			out = MakeIntVector3(int(std::floor(hit.x + 0.5F)), int(std::floor(hit.y + 0.5F)),
			                     int(std::floor(hit.z + 0.5F)));
			return true;
		}

		void VoxelEditor::DoPick() {
			pickHit = false;
			const GizmoView view = DocumentView();
			if (!view.IsValid())
				return;
			Vector3 eye, dir;
			view.Ray(host.AimPosition(), eye, dir);
			pickHit = FirstSolidAlong(eye, dir, pickSolid, pickPlace);
		}

		bool VoxelEditor::FirstSolidAlong(const Vector3& eye, const Vector3& dir, IntVector3& solid,
		                                  IntVector3& before) const {
			// The renderer centres voxel (x,y,z) at world (x,y,z), so traverse in a
			// +0.5-shifted space where each integer cell maps to a voxel index.
			Vector3 o = eye + MakeVector3(0.5F, 0.5F, 0.5F);

			// Only the volume can hold a voxel: clip the ray to its box and walk
			// from where it enters. Walking from the eye instead would cost steps
			// in proportion to the camera's distance, and with any fixed budget a
			// small volume seen from afar would never be reached at all.
			const IntVector3 extent = document.Size();
			const float size[3] = {float(extent.x), float(extent.y), float(extent.z)};
			const float from[3] = {o.x, o.y, o.z};
			const float along[3] = {dir.x, dir.y, dir.z};
			float tEnter = 0.0F, tExit = std::numeric_limits<float>::infinity();
			for (int a = 0; a < 3; a++) {
				if (along[a] == 0.0F) {
					if (from[a] < 0.0F || from[a] > size[a])
						return false; // parallel to this slab and outside it
					continue;
				}
				float t0 = (0.0F - from[a]) / along[a];
				float t1 = (size[a] - from[a]) / along[a];
				if (t0 > t1)
					std::swap(t0, t1);
				tEnter = std::max(tEnter, t0);
				tExit = std::min(tExit, t1);
			}
			if (tEnter > tExit)
				return false; // the ray misses the volume
			// The cell before the first one inside is the one a voxel is placed
			// in when the hit is on the volume's own face.
			const float kInside = 1.0e-4F;
			o = o + dir * std::max(0.0F, tEnter - kInside);

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

			float tMaxX =
			  (dir.x != 0.0F) ? ((float(cx) + (stepX > 0 ? 1.0F : 0.0F) - o.x) / dir.x) : big;
			float tMaxY =
			  (dir.y != 0.0F) ? ((float(cy) + (stepY > 0 ? 1.0F : 0.0F) - o.y) / dir.y) : big;
			float tMaxZ =
			  (dir.z != 0.0F) ? ((float(cz) + (stepZ > 0 ? 1.0F : 0.0F) - o.z) / dir.z) : big;

			// A straight line crosses at most one cell per unit of each axis, so
			// this many steps take it from one side of the volume to the other.
			int px = cx, py = cy, pz = cz;
			const int limit = extent.x + extent.y + extent.z + 3;
			for (int i = 0; i < limit; i++) {
				if (document.IsSolid(cx, cy, cz)) {
					solid = MakeIntVector3(cx, cy, cz);
					before = MakeIntVector3(px, py, pz);
					return true;
				}
				px = cx;
				py = cy;
				pz = cz;
				if (tMaxX <= tMaxY && tMaxX <= tMaxZ) {
					cx += stepX;
					tMaxX += tDeltaX;
				} else if (tMaxY <= tMaxZ) {
					cy += stepY;
					tMaxY += tDeltaY;
				} else {
					cz += stepZ;
					tMaxZ += tDeltaZ;
				}
			}
			return false;
		}

		// --- Volume -----------------------------------------------------------

		VoxelEditor::VolumeFrame VoxelEditor::FrameHolding(const IntVector3& lo,
		                                                   const IntVector3& hi) const {
			const IntVector3 size = document.Size();
			VolumeFrame frame;
			if (!document.Resizable()) {
				// A fixed volume holds what it holds; anything past it is clipped.
				frame.size = size;
				frame.shift = MakeIntVector3(0, 0, 0);
				return frame;
			}
			const IntVector3 from =
			  MakeIntVector3(std::min(0, lo.x), std::min(0, lo.y), std::min(0, lo.z));
			const IntVector3 to = MakeIntVector3(std::max(size.x, hi.x + 1),
			                                     std::max(size.y, hi.y + 1),
			                                     std::max(size.z, hi.z + 1));
			frame.size = to - from;
			frame.shift = MakeIntVector3(-from.x, -from.y, -from.z);
			return frame;
		}

		bool VoxelEditor::Fits(const VolumeFrame& frame) const {
			const IntVector3 limit = document.MaxSize();
			return frame.size.x <= limit.x && frame.size.y <= limit.y && frame.size.z <= limit.z;
		}

		void VoxelEditor::Reframe(const VolumeFrame& frame) {
			if (frame.shift == MakeIntVector3(0, 0, 0) && frame.size == document.Size())
				return;
			RebuildVolume(frame.size, frame.shift);
		}

		void VoxelEditor::RebuildVolume(const IntVector3& size, const IntVector3& shift) {
			const IntVector3 before = document.Size();
			undo.RecordReframe(before.x, before.y, before.z, size.x, size.y, size.z, shift.x,
			                   shift.y, shift.z);
			ReframeRaw(size, shift);
		}

		void VoxelEditor::ReframeRaw(const IntVector3& size, const IntVector3& offset) {
			document.Reframe(size, offset);
			const Vector3 shift = MakeVector3(float(offset.x), float(offset.y), float(offset.z));
			host.DocumentReframed(shift); // the view stays on the relabelled voxels
			// Everything else held in voxel coordinates follows the relabelling:
			// the mirror planes (by whole voxels, so still on the half-step
			// grid), the reference images, the selection, and pending voxels,
			// which would otherwise land in the wrong place.
			edit.mirror.plane += shift;
			// An image is lined up with the voxels it is being modelled from, so
			// it goes where they go rather than letting the model slide out from
			// under it every time the volume grows or is trimmed.
			for (ReferenceImage& ref : references)
				ref.centre += shift;
			// The middle a moved selection turns about goes with it, and stays
			// its middle: the shifted selection is the same one, relabelled.
			const bool middleKept =
			  movedMiddle.valid && movedMiddle.selectionVersion == edit.selection.Version();
			edit.selection.Shift(offset);
			if (middleKept) {
				movedMiddle.selectionVersion = edit.selection.Version();
				movedMiddle.at = movedMiddle.at + offset;
			}
			if (edit.placing) {
				PendingPlacement& pending = edit.placement;
				pending.anchor = pending.anchor + offset;
				pending.pivot = pending.pivot + offset;
			}
		}

		void VoxelEditor::TrimVolume() {
			if (!document.Resizable())
				return;
			const IntVector3 size = document.Size();
			int minX = size.x, minY = size.y, minZ = size.z;
			int maxX = -1, maxY = -1, maxZ = -1;
			for (int x = 0; x < size.x; x++)
				for (int y = 0; y < size.y; y++)
					for (int z = 0; z < size.z; z++) {
						if (!document.IsSolid(x, y, z))
							continue;
						minX = std::min(minX, x);
						minY = std::min(minY, y);
						minZ = std::min(minZ, z);
						maxX = std::max(maxX, x);
						maxY = std::max(maxY, y);
						maxZ = std::max(maxZ, z);
					}
			if (maxX < 0)
				return;
			const IntVector3 trimmed = MakeIntVector3(maxX - minX + 1, maxY - minY + 1, maxZ - minZ + 1);
			if (trimmed == size)
				return;
			RebuildVolume(trimmed, MakeIntVector3(-minX, -minY, -minZ));
		}
	} // namespace gui
} // namespace spades
