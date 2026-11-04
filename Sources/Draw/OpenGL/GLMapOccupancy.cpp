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
#include <cstdint>
#include <tuple>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "GLMapOccupancy.h"
#include "GLRenderer.h"
#include <Client/GameMap.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		namespace {
			/**
			 * The most clearance a block records, which is also the clearance of every
			 * block with no solid block within `kMaxClearance - 1` of it.
			 */
			constexpr int kMaxClearance = 8;

			/** The edge of a region uploaded at once, in blocks. */
			constexpr int kRegionSize = 16;

			// Every region is whole, and its rows meet the default unpack alignment.
			static_assert(client::GameMap::DefaultWidth % kRegionSize == 0 &&
			                client::GameMap::DefaultHeight % kRegionSize == 0 &&
			                client::GameMap::DefaultDepth % kRegionSize == 0,
			              "the map must be made of whole regions");
			static_assert(kRegionSize % 4 == 0, "region rows must be 4-byte aligned");

			// A block's change reaches the blocks within `kMaxClearance - 1` of it, which
			// lie in at most two regions along each axis.
			static_assert(kMaxClearance - 1 < kRegionSize, "a change must reach at most two regions");

			/** The bottom layer of a column, which the solid ground below the map reaches
			 * in one step. */
			constexpr std::uint64_t kBottomLayer = 1ULL << (client::GameMap::DefaultDepth - 1);
			static_assert(client::GameMap::DefaultDepth == 64, "a column must be 64 blocks deep");

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

		GLMapOccupancy::GLMapOccupancy(GLRenderer& renderer, const client::GameMap& map)
		    : device(renderer.GetGLDevice()), map(map) {
			SPADES_MARK_FUNCTION();

			const IntVector3 size = MakeIntVector3(map.Width(), map.Height(), map.Depth());
			isRegionDirty.resize((std::size_t)(size.x / kRegionSize) * (size.y / kRegionSize) *
			                     (size.z / kRegionSize));

			std::vector<std::uint8_t> texels;
			ComputeClearance(0, 0, size.x, size.y, texels);

			// Sized for the whole map, much more than any region needs later
			grown = {};
			next = {};
			reached = {};

			texture = device.GenTexture();
			device.BindTexture(IGLDevice::Texture3D, texture);
			// One block per texel: nothing in between to filter, and no mipmaps.
			device.TexParamater(IGLDevice::Texture3D, IGLDevice::TextureMagFilter,
			                    IGLDevice::Nearest);
			device.TexParamater(IGLDevice::Texture3D, IGLDevice::TextureMinFilter,
			                    IGLDevice::Nearest);
			device.TexParamater(IGLDevice::Texture3D, IGLDevice::TextureWrapS, IGLDevice::Repeat);
			device.TexParamater(IGLDevice::Texture3D, IGLDevice::TextureWrapT, IGLDevice::Repeat);
			device.TexParamater(IGLDevice::Texture3D, IGLDevice::TextureWrapR,
			                    IGLDevice::ClampToEdge);
			device.TexImage3D(IGLDevice::Texture3D, 0, IGLDevice::Red, size.x, size.y, size.z, 0,
			                  IGLDevice::Red, IGLDevice::UnsignedByte, texels.data());
		}

		GLMapOccupancy::~GLMapOccupancy() { device.DeleteTexture(texture); }

		int GLMapOccupancy::RegionIndex(const IntVector3& region) const {
			const int regionsX = map.Width() / kRegionSize;
			const int regionsY = map.Height() / kRegionSize;
			return region.x + (region.y + region.z * regionsY) * regionsX;
		}

		void GLMapOccupancy::ComputeClearance(int x0, int y0, int width, int height,
		                                      std::vector<std::uint8_t>& texels) const {
			// The solid blocks are grown by one block in every direction at a time, a
			// whole column of 64 at once: a block's clearance is the number of steps
			// before they reach it. A block's clearance only depends on the blocks
			// within `kMaxClearance - 1` of it, so the columns that far around the box
			// are grown with it.
			const int margin = kMaxClearance - 1;
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
			texels.assign(layerSize * map.Depth(), (std::uint8_t)kMaxClearance);
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
							texels[column + (std::size_t)CountTrailingZeros(bits) * layerSize] =
							  (std::uint8_t)step;
					}
				}

				if (step == kMaxClearance - 1)
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

		Vector3 GLMapOccupancy::GetSize() const {
			return MakeVector3((float)map.Width(), (float)map.Height(), (float)map.Depth());
		}

		void GLMapOccupancy::MarkRegionDirty(const IntVector3& region) {
			const int index = RegionIndex(region);
			if (isRegionDirty[index])
				return;
			isRegionDirty[index] = true;
			dirtyRegions.push_back(region);
		}

		void GLMapOccupancy::GameMapChanged(int x, int y, int z) {
			if (x < 0 || y < 0 || z < 0 || x >= map.Width() || y >= map.Height() ||
			    z >= map.Depth())
				return;

			// Every block whose clearance the change can alter
			const int reach = kMaxClearance - 1;
			const int xs[] = {Wrap(x - reach, map.Width()) / kRegionSize,
			                  Wrap(x + reach, map.Width()) / kRegionSize};
			const int ys[] = {Wrap(y - reach, map.Height()) / kRegionSize,
			                  Wrap(y + reach, map.Height()) / kRegionSize};
			const int zs[] = {std::max(z - reach, 0) / kRegionSize,
			                  std::min(z + reach, map.Depth() - 1) / kRegionSize};
			for (int rz : zs)
				for (int ry : ys)
					for (int rx : xs)
						MarkRegionDirty(MakeIntVector3(rx, ry, rz));
		}

		void GLMapOccupancy::Update() {
			if (dirtyRegions.empty())
				return;

			// The regions of a column of regions are grown together, at once.
			std::sort(dirtyRegions.begin(), dirtyRegions.end(),
			          [](const IntVector3& a, const IntVector3& b) {
				          return std::tie(a.y, a.x, a.z) < std::tie(b.y, b.x, b.z);
			          });

			const std::size_t layerSize = (std::size_t)kRegionSize * kRegionSize;

			device.BindTexture(IGLDevice::Texture3D, texture);
			for (std::size_t i = 0; i < dirtyRegions.size();) {
				const IntVector3 column = dirtyRegions[i];
				ComputeClearance(column.x * kRegionSize, column.y * kRegionSize, kRegionSize,
				                 kRegionSize, columnTexels);

				for (; i < dirtyRegions.size() && dirtyRegions[i].x == column.x &&
				       dirtyRegions[i].y == column.y;
				     i++) {
					const IntVector3 origin = dirtyRegions[i] * kRegionSize;
					device.TexSubImage3D(IGLDevice::Texture3D, 0, origin.x, origin.y, origin.z,
					                     kRegionSize, kRegionSize, kRegionSize, IGLDevice::Red,
					                     IGLDevice::UnsignedByte,
					                     columnTexels.data() + origin.z * layerSize);
					isRegionDirty[RegionIndex(dirtyRegions[i])] = false;
				}
			}
			dirtyRegions.clear();
		}
	} // namespace draw
} // namespace spades
