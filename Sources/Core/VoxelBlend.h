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

#include "Math.h"
#include "RefCountedObject.h"

namespace spades {
	class VoxelModel;

	/**
	 * How the solid voxels of one model combine with another's (see
	 * BlendVoxels), the voxel booleans of the model tools.
	 */
	enum class VoxelBlend {
		Over,      // union: the source's voxels, over the target's
		Under,     // union: the source's voxels, only into the target's air
		Carve,     // subtraction: the target loses the voxels the source covers
		Paint,     // the target's voxels the source covers take the source's colours
		Intersect, // intersection: the target keeps only the voxels the source covers
	};

	/**
	 * Combines `source` into `target`, the source's voxel (0, 0, 0) landing on
	 * the target's `at`. With `grow`, the target's volume grows to hold all of
	 * the source first (only a union adds voxels, so only a union needs it),
	 * its pivot staying on the same voxels; otherwise what lands outside it is
	 * left out. Returns the combined model, which is `target` unless it grew.
	 */
	Handle<VoxelModel> BlendVoxels(Handle<VoxelModel> target, const VoxelModel& source,
	                               const IntVector3& at, VoxelBlend mode, bool grow);

	/**
	 * A copy of `model` in a volume of `size`, each voxel moved by `shift`,
	 * the pivot staying on the same voxels; what falls outside is left out.
	 */
	Handle<VoxelModel> ReframedVoxelModel(const VoxelModel& model, const IntVector3& size,
	                                      const IntVector3& shift);
} // namespace spades
