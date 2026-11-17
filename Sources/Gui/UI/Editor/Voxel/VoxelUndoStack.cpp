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

#include "VoxelUndoStack.h"

namespace spades {
	namespace gui {
		void VoxelUndoStack::Sink::UndoApply(const VoxelUndoRecord& r, bool forward) {
			switch (r.kind) {
				case VoxelUndoRecord::Kind::Voxel:
					if (forward)
						UndoApplyVoxel(r.v.x, r.v.y, r.v.z, r.v.newSolid, r.v.newColor);
					else
						UndoApplyVoxel(r.v.x, r.v.y, r.v.z, r.v.oldSolid, r.v.oldColor);
					break;
				case VoxelUndoRecord::Kind::Reframe:
					// Backward: back to the old size, with the shift undone.
					if (forward)
						UndoApplyReframe(r.r.aw, r.r.ah, r.r.ad, r.r.ox, r.r.oy, r.r.oz);
					else
						UndoApplyReframe(r.r.bw, r.r.bh, r.r.bd, -r.r.ox, -r.r.oy, -r.r.oz);
					break;
				case VoxelUndoRecord::Kind::Origin:
					if (forward)
						UndoApplyOrigin(MakeVector3(r.o.ax, r.o.ay, r.o.az));
					else
						UndoApplyOrigin(MakeVector3(r.o.bx, r.o.by, r.o.bz));
					break;
			}
		}

		void VoxelUndoStack::RecordVoxel(int x, int y, int z, bool oldSolid, std::uint32_t oldColor,
		                                 bool newSolid, std::uint32_t newColor) {
			if (oldSolid == newSolid && oldColor == newColor)
				return; // no change
			VoxelUndoRecord rec;
			rec.kind = VoxelUndoRecord::Kind::Voxel;
			rec.v = {x, y, z, oldColor, newColor, oldSolid, newSolid};
			Append(rec);
		}

		void VoxelUndoStack::RecordReframe(int beforeW, int beforeH, int beforeD, int afterW,
		                                   int afterH, int afterD, int ox, int oy, int oz) {
			VoxelUndoRecord rec;
			rec.kind = VoxelUndoRecord::Kind::Reframe;
			rec.r = {beforeW, beforeH, beforeD, afterW, afterH, afterD, ox, oy, oz};
			Append(rec);
		}

		void VoxelUndoStack::RecordOrigin(const Vector3& before, const Vector3& after) {
			if (before.x == after.x && before.y == after.y && before.z == after.z)
				return; // no change
			VoxelUndoRecord rec;
			rec.kind = VoxelUndoRecord::Kind::Origin;
			rec.o = {before.x, before.y, before.z, after.x, after.y, after.z};
			Append(rec);
		}
	} // namespace gui
} // namespace spades
