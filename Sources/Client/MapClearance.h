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
#include <vector>

#include <Core/Math.h>

namespace spades {
	namespace client {
		class GameMap;

		/**
		 * How clear of solid blocks each block of a map is, for renderers to keep as a
		 * 3D texture that shaders walk rays through a clear cube at a time: a solid
		 * block has a clearance of 0, and around a block with a clearance `c`, every
		 * block within `c - 1` of it is clear. The map wraps around horizontally,
		 * above it is open sky, and below it solid ground.
		 *
		 * It keeps track of the regions of the map whose clearance blocks built and
		 * destroyed have changed, for the renderer to upload again.
		 */
		class MapClearance {
		public:
			/**
			 * The most clearance a block records, which is also the clearance of every
			 * block with no solid block within `MaxClearance - 1` of it.
			 */
			static constexpr int MaxClearance = 8;

			/** The edge of a region of the map tracked and handed out at once, in
			 * blocks. */
			static constexpr int RegionSize = 16;

			/** The blocks of a region, `RegionSize` cubed */
			static constexpr int RegionVolume = RegionSize * RegionSize * RegionSize;

			explicit MapClearance(const GameMap&);

			MapClearance(const MapClearance&) = delete;
			MapClearance& operator=(const MapClearance&) = delete;

			/** The size of the map in blocks. */
			IntVector3 GetSize() const;

			/**
			 * Fills `clearance` with the clearance of every block of the map, along x,
			 * then y, then z. Changes noted before are left to `TakeChangedRegions`.
			 */
			void ComputeAll(std::vector<std::uint8_t>& clearance);

			/** Notes that the block at `x`, `y`, `z` was built or destroyed. */
			void GameMapChanged(int x, int y, int z);

			bool HasChangedRegions() const { return !changedRegions.empty(); }

			/**
			 * Calls `visit` with the origin, in blocks, of every region changed since
			 * the last call, and its clearance as it is now, `RegionVolume` values
			 * along x, then y, then z, which are only valid during the call. Stops,
			 * keeping the rest for the next call, once `visit` returns `false`.
			 */
			void TakeChangedRegions(
			  const std::function<bool(const IntVector3& origin, const std::uint8_t* clearance)>&
			    visit);

		private:
			const GameMap& map;

			/** The regions changed since they were last taken, each at most once */
			std::vector<IntVector3> changedRegions;
			std::vector<bool> isRegionChanged;

			int RegionIndex(const IntVector3& region) const;
			void MarkRegionChanged(const IntVector3& region);

			/** Fills `clearance` with that of the blocks of the `width` by `height`
			 * columns from `x0`, `y0` on, the whole map deep, along x, then y, then
			 * z. */
			void ComputeClearance(int x0, int y0, int width, int height,
			                      std::vector<std::uint8_t>& clearance);

			// Kept from call to call, not to allocate them again for every region
			std::vector<std::uint64_t> grown;
			std::vector<std::uint64_t> next;
			std::vector<std::uint64_t> reached;
			std::vector<std::uint8_t> columnClearance;
		};
	} // namespace client
} // namespace spades
