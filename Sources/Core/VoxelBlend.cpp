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

#include "VoxelBlend.h"

#include <algorithm>
#include <cstdint>

#include "VoxelModel.h"

namespace spades {
	Handle<VoxelModel> ReframedVoxelModel(const VoxelModel& m, const IntVector3& size,
	                                      const IntVector3& shift) {
		Handle<VoxelModel> out = Handle<VoxelModel>::New(size.x, size.y, size.z);
		for (int x = 0; x < m.GetWidth(); x++)
			for (int y = 0; y < m.GetHeight(); y++)
				for (int z = 0; z < m.GetDepth(); z++) {
					if (!m.IsSolid(x, y, z))
						continue;
					const int nx = x + shift.x, ny = y + shift.y, nz = z + shift.z;
					if (nx < 0 || ny < 0 || nz < 0 || nx >= size.x || ny >= size.y || nz >= size.z)
						continue;
					out->SetSolid(nx, ny, nz, m.GetColor(x, y, z) & 0xFFFFFF);
				}
		out->SetOrigin(m.GetOrigin() - MakeVector3(float(shift.x), float(shift.y), float(shift.z)));
		return out;
	}

	Handle<VoxelModel> BlendVoxels(Handle<VoxelModel> target, const VoxelModel& src,
	                               const IntVector3& atIn, VoxelBlend mode, bool grow) {
		IntVector3 at = atIn;
		const bool adds = mode == VoxelBlend::Over || mode == VoxelBlend::Under;
		if (grow && adds) {
			// The smallest volume holding both, the target's voxels moved into it.
			const IntVector3 lo = MakeIntVector3(std::min(0, at.x), std::min(0, at.y), std::min(0, at.z));
			const IntVector3 hi =
			  MakeIntVector3(std::max(target->GetWidth(), at.x + src.GetWidth()),
			                 std::max(target->GetHeight(), at.y + src.GetHeight()),
			                 std::max(target->GetDepth(), at.z + src.GetDepth()));
			const IntVector3 shift = MakeIntVector3(-lo.x, -lo.y, -lo.z);
			if (lo.x < 0 || lo.y < 0 || lo.z < 0 || hi.x > target->GetWidth() ||
			    hi.y > target->GetHeight() || hi.z > target->GetDepth()) {
				target = ReframedVoxelModel(*target, hi - lo, shift);
				at = at + shift;
			}
		}
		VoxelModel& m = *target;
		auto sourceSolid = [&](int tx, int ty, int tz) {
			const int sx = tx - at.x, sy = ty - at.y, sz = tz - at.z;
			return sx >= 0 && sy >= 0 && sz >= 0 && sx < src.GetWidth() && sy < src.GetHeight() &&
			       sz < src.GetDepth() && src.IsSolid(sx, sy, sz);
		};
		if (mode == VoxelBlend::Intersect) {
			// What the source does not cover goes, wherever it is.
			for (int x = 0; x < m.GetWidth(); x++)
				for (int y = 0; y < m.GetHeight(); y++)
					for (int z = 0; z < m.GetDepth(); z++) {
						if (m.IsSolid(x, y, z) && !sourceSolid(x, y, z))
							m.SetAir(x, y, z);
					}
			return target;
		}
		for (int x = 0; x < src.GetWidth(); x++)
			for (int y = 0; y < src.GetHeight(); y++)
				for (int z = 0; z < src.GetDepth(); z++) {
					if (!src.IsSolid(x, y, z))
						continue;
					const int tx = x + at.x, ty = y + at.y, tz = z + at.z;
					if (tx < 0 || ty < 0 || tz < 0 || tx >= m.GetWidth() || ty >= m.GetHeight() ||
					    tz >= m.GetDepth())
						continue; // outside the target: left out
					const std::uint32_t color = src.GetColor(x, y, z) & 0xFFFFFF;
					const bool solid = m.IsSolid(tx, ty, tz);
					switch (mode) {
						case VoxelBlend::Over: m.SetSolid(tx, ty, tz, color); break;
						case VoxelBlend::Under:
							if (!solid)
								m.SetSolid(tx, ty, tz, color);
							break;
						case VoxelBlend::Carve: m.SetAir(tx, ty, tz); break;
						case VoxelBlend::Paint:
							if (solid)
								m.SetSolid(tx, ty, tz, color);
							break;
						case VoxelBlend::Intersect: break; // done above
					}
				}
		return target;
	}
} // namespace spades
