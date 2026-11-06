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

#include <string>
#include <vector>

#include "Math.h"
#include "RefCountedObject.h"
#include "VoxelModel2KV6.h"

namespace spades {
	class VoxelModel;

	/**
	 * How many voxels of a model to make for each voxel of a scene, each way:
	 * `finer` of them for every `coarser`, in lowest terms. 4/1 is four times
	 * finer, 1/4 four times coarser, 3/2 one and a half times finer; 1/1 is
	 * the scene as the game composes it.
	 */
	struct VoxelResolution {
		/** The furthest the editor's steps go either way: 5/1 and 1/5. */
		static constexpr int kMaxStep = 5;

		int finer = 1;
		int coarser = 1;

		/** `finer` for every `coarser`, in lowest terms; 1/1 for anything
		 *  not above zero. */
		static VoxelResolution Of(int finer, int coarser);
		static VoxelResolution Finer(int n) { return Of(n, 1); }
		static VoxelResolution Coarser(int n) { return Of(1, n); }
		/** From 1/kMaxStep to kMaxStep/1 in halves, coarsest first: 1/5,
		 *  1/4.5, ..., 1/1.5, 1/1, 1.5/1, ..., 5/1. */
		static const std::vector<VoxelResolution>& Steps();
		/** "4/1", "1/2.5", "1.5/1", or a count alone for that many to 1;
		 *  false for anything else. Counts take one decimal at most. */
		static bool Parse(const std::string& text, VoxelResolution& out);

		/** One voxel of the result, in voxels of the scene: 1/4 for 4/1. */
		float VoxelSize() const { return float(coarser) / float(finer); }
		/** Whether it is a whole number of times either way (4/1, 1/3). */
		bool Whole() const { return finer == 1 || coarser == 1; }
		/** "4/1", "1/1", "1/2.5", "1.5/1": one side 1, as the editor shows it. */
		std::string Label() const;
		/** The size a model of `size` voxels comes to, each side rounded up. */
		IntVector3 SizeOf(const IntVector3& size) const;

		bool operator==(const VoxelResolution& o) const {
			return finer == o.finer && coarser == o.coarser;
		}
		bool operator!=(const VoxelResolution& o) const { return !(*this == o); }
	};

	/**
	 * The objects of a scene (and all under them, the hidden left out, as the
	 * game leaves them out) made into one model at `resolution`, in the
	 * scene's frame:
	 *
	 *  - 1/1 is the scene as the game composes it (MergeScene).
	 *  - Coarser by a whole number (1/3) is that made coarser
	 *    (DownsampledVoxelModel): plates and rods a few voxels thick survive.
	 *  - Finer, or coarser by a step between (1/2.5), samples each object
	 *    through its own transform: each voxel of
	 *    the result takes the colour of the object's voxel its middle falls
	 *    in, the later object over the earlier where they meet. A part the
	 *    scene scales down (finer than the rest) keeps its detail.
	 *
	 * `frame` gets where the result's voxel `i` lies in the scene: its middle
	 * at `frame * i`. The model's pivot is the scene's origin. Null, `why`
	 * saying why, when nothing of it is left or it would be past what a KV6
	 * holds (64 voxels deep, 4096 across).
	 */
	Handle<VoxelModel> ResampledScene(const std::vector<VoxelObject>& objects,
	                                  const VoxelResolution& resolution, Matrix4& frame,
	                                  std::string& why);
} // namespace spades
