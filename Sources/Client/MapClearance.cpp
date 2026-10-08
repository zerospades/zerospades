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

#include <algorithm>
#include <tuple>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "GameMap.h"
#include "MapClearance.h"
#include <Core/Debug.h>

namespace spades {
	namespace client {
		namespace {
			// Every region is whole.
			static_assert(GameMap::DefaultWidth % MapClearance::RegionSize == 0 &&
			                GameMap::DefaultHeight % MapClearance::RegionSize == 0 &&
			                GameMap::DefaultDepth % MapClearance::RegionSize == 0,
			              "the map must be made of whole regions");

			// A block's change reaches the blocks within `MaxClearance - 1` of it, which
			// lie in at most two regions along each axis.
			static_assert(MapClearance::MaxClearance - 1 < MapClearance::RegionSize,
			              "a change must reach at most two regions");

			/** The bottom layer of a column, which the solid ground below the map reaches
			 * in one step. */
			constexpr std::uint64_t kBottomLayer = 1ULL << (GameMap::DefaultDepth - 1);
			static_assert(GameMap::DefaultDepth == 64, "a column must be 64 blocks deep");

			/** The index of the lowest set bit of `v`, which must not be zero. */
			int CountTrailingZeros(std::uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
				return __builtin_ctzll(v);
#elif defined(_MSC_VER) && defined(_WIN64)
				unsigned long index;
				_BitScanForward64(&index, v);
				return (int)index;
#else
				int index = 0;
				for (; !(v & 1); v >>= 1)
					index++;
				return index;
#endif
			}

			/** `v` brought into `[0, size)`, the map wrapping around horizontally. */
			int Wrap(int v, int size) { return ((v % size) + size) % size; }
		} // namespace

		MapClearance::MapClearance(const GameMap& map) : map(map) {
			const IntVector3 size = GetSize();
			isRegionChanged.resize((std::size_t)(size.x / RegionSize) * (size.y / RegionSize) *
			                       (size.z / RegionSize));
		}

		IntVector3 MapClearance::GetSize() const {
			return MakeIntVector3(map.Width(), map.Height(), map.Depth());
		}

		int MapClearance::RegionIndex(const IntVector3& region) const {
			const int regionsX = map.Width() / RegionSize;
			const int regionsY = map.Height() / RegionSize;
			return region.x + (region.y + region.z * regionsY) * regionsX;
		}

		void MapClearance::ComputeAll(std::vector<std::uint8_t>& clearance) {
			SPADES_MARK_FUNCTION();

			ComputeClearance(0, 0, map.Width(), map.Height(), clearance);

			// Sized for the whole map, much more than any region needs later
			grown = {};
			next = {};
			reached = {};
		}

		void MapClearance::ComputeClearance(int x0, int y0, int width, int height,
		                                    std::vector<std::uint8_t>& clearance) {
			// The solid blocks are grown by one block in every direction at a time, a
			// whole column of 64 at once: a block's clearance is the number of steps
			// before they reach it. A block's clearance only depends on the blocks
			// within `MaxClearance - 1` of it, so the columns that far around the box
			// are grown with it.
			const int margin = MaxClearance - 1;
			const int gridWidth = width + 2 * margin;
			const int gridHeight = height + 2 * margin;
			grown.assign((std::size_t)gridWidth * gridHeight, 0);
			next.assign(grown.size(), 0);

			for (int gy = 0; gy < gridHeight; gy++) {
				const int y = Wrap(y0 - margin + gy, map.Height());
				for (int gx = 0; gx < gridWidth; gx++) {
					const int x = Wrap(x0 - margin + gx, map.Width());
					grown[gx + (std::size_t)gy * gridWidth] = map.GetSolidMap(x, y);
				}
			}

			const std::size_t layerSize = (std::size_t)width * height;
			clearance.assign(layerSize * map.Depth(), (std::uint8_t)MaxClearance);
			reached.assign(layerSize, 0);

			for (int step = 0;; step++) {
				// The blocks the solid ones reached in this many steps, and no fewer
				for (int y = 0; y < height; y++) {
					for (int x = 0; x < width; x++) {
						const std::size_t column = x + (std::size_t)y * width;
						std::uint64_t bits =
						  grown[(x + margin) + (std::size_t)(y + margin) * gridWidth] &
						  ~reached[column];
						reached[column] |= bits;
						for (; bits; bits &= bits - 1)
							clearance[column + (std::size_t)CountTrailingZeros(bits) * layerSize] =
							  (std::uint8_t)step;
					}
				}

				if (step == MaxClearance - 1)
					break;

				// One more step. The grid's edge grows wrong, but no further in than the
				// steps taken, which stay within the margin. Below the map is solid, as
				// `GameMap` has it, so the bottom layer is reached from there.
				for (int gy = 1; gy < gridHeight - 1; gy++) {
					for (int gx = 1; gx < gridWidth - 1; gx++) {
						std::uint64_t column = 0;
						for (int dy = -1; dy <= 1; dy++)
							for (int dx = -1; dx <= 1; dx++)
								column |= grown[(gx + dx) + (std::size_t)(gy + dy) * gridWidth];
						next[gx + (std::size_t)gy * gridWidth] =
						  column | (column << 1) | (column >> 1) | kBottomLayer;
					}
				}
				grown.swap(next);
			}
		}

		void MapClearance::MarkRegionChanged(const IntVector3& region) {
			const int index = RegionIndex(region);
			if (isRegionChanged[index])
				return;
			isRegionChanged[index] = true;
			changedRegions.push_back(region);
		}

		void MapClearance::GameMapChanged(int x, int y, int z) {
			if (x < 0 || y < 0 || z < 0 || x >= map.Width() || y >= map.Height() ||
			    z >= map.Depth())
				return;

			// Every block whose clearance the change can alter
			const int reach = MaxClearance - 1;
			const int xs[] = {Wrap(x - reach, map.Width()) / RegionSize,
			                  Wrap(x + reach, map.Width()) / RegionSize};
			const int ys[] = {Wrap(y - reach, map.Height()) / RegionSize,
			                  Wrap(y + reach, map.Height()) / RegionSize};
			const int zs[] = {std::max(z - reach, 0) / RegionSize,
			                  std::min(z + reach, map.Depth() - 1) / RegionSize};
			for (int rz : zs)
				for (int ry : ys)
					for (int rx : xs)
						MarkRegionChanged(MakeIntVector3(rx, ry, rz));
		}

		void MapClearance::TakeChangedRegions(
		  const std::function<bool(const IntVector3& origin, const std::uint8_t* clearance)>&
		    visit) {
			if (changedRegions.empty())
				return;

			// The regions of a column of regions are grown together, at once.
			std::sort(changedRegions.begin(), changedRegions.end(),
			          [](const IntVector3& a, const IntVector3& b) {
				          return std::tie(a.y, a.x, a.z) < std::tie(b.y, b.x, b.z);
			          });

			// A column's layers are `RegionSize` squared each, so a region's are
			// together in it.
			const std::size_t layerSize = (std::size_t)RegionSize * RegionSize;

			std::size_t taken = 0;
			bool more = true;
			while (more && taken < changedRegions.size()) {
				const IntVector3 column = changedRegions[taken];
				ComputeClearance(column.x * RegionSize, column.y * RegionSize, RegionSize,
				                 RegionSize, columnClearance);

				for (; taken < changedRegions.size() && changedRegions[taken].x == column.x &&
				       changedRegions[taken].y == column.y;
				     taken++) {
					const IntVector3 origin = changedRegions[taken] * RegionSize;
					if (!visit(origin, columnClearance.data() + origin.z * layerSize)) {
						more = false;
						break;
					}
					isRegionChanged[RegionIndex(changedRegions[taken])] = false;
				}
			}
			changedRegions.erase(changedRegions.begin(), changedRegions.begin() + taken);
		}
	} // namespace client
} // namespace spades
