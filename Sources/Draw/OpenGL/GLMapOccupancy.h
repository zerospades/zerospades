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

#include "IGLDevice.h"
#include <Core/Math.h>

namespace spades {
	namespace client {
		class GameMap;
	}
	namespace draw {
		class GLRenderer;

		/**
		 * How clear of solid blocks each block of the map is, as a 3D texture for
		 * shaders to walk rays through: one texel per block, holding the block's
		 * clearance `c` out of 255. A solid block has none; around a block with a
		 * clearance `c`, every block within `c - 1` of it is clear, so a ray can cross
		 * all of them in one go. It wraps horizontally like the map, and is kept in
		 * step with it as blocks are built and destroyed.
		 */
		class GLMapOccupancy {
			IGLDevice& device;
			const client::GameMap& map;
			IGLDevice::UInteger texture;

			/**
			 * Regions holding blocks changed since the last `Update`, at most once
			 * each. Each one is uploaded whole in a single call: a collapsing
			 * structure or a grenade changes many blocks at once, but few regions.
			 */
			std::vector<IntVector3> dirtyRegions;
			std::vector<bool> isRegionDirty;

			int RegionIndex(const IntVector3& region) const;
			void MarkRegionDirty(const IntVector3& region);

			/** Fills `texels` with the clearance of the blocks of the `width` by
			 * `height` columns from `x0`, `y0` on, the whole map deep, running along x,
			 * then y, then z, as the texture's do. */
			void ComputeClearance(int x0, int y0, int width, int height,
			                      std::vector<std::uint8_t>& texels) const;

			// Kept from call to call, not to allocate them again for every region
			mutable std::vector<std::uint64_t> grown;
			mutable std::vector<std::uint64_t> next;
			mutable std::vector<std::uint64_t> reached;
			std::vector<std::uint8_t> columnTexels;

		public:
			GLMapOccupancy(GLRenderer&, const client::GameMap&);
			~GLMapOccupancy();

			GLMapOccupancy(const GLMapOccupancy&) = delete;
			GLMapOccupancy& operator=(const GLMapOccupancy&) = delete;

			void GameMapChanged(int x, int y, int z);

			/** Uploads the blocks changed since the last call. */
			void Update();

			IGLDevice::UInteger GetTexture() const { return texture; }

			/** The map's size in blocks, which is the texture's in texels. */
			Vector3 GetSize() const;
		};
	} // namespace draw
} // namespace spades
