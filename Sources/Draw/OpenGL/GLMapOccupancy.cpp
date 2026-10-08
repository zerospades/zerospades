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

#include <cstdint>

#include "GLMapOccupancy.h"
#include "GLRenderer.h"
#include <Client/GameMap.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		namespace {
			const std::uint8_t kSolid = 255;
			const std::uint8_t kEmpty = 0;

			/** The edge of a region uploaded at once, in blocks. */
			constexpr int kRegionSize = 16;

			// Every region is whole, and its rows meet the default unpack alignment.
			static_assert(client::GameMap::DefaultWidth % kRegionSize == 0 &&
			                client::GameMap::DefaultHeight % kRegionSize == 0 &&
			                client::GameMap::DefaultDepth % kRegionSize == 0,
			              "the map must be made of whole regions");
			static_assert(kRegionSize % 4 == 0, "region rows must be 4-byte aligned");
		} // namespace

		GLMapOccupancy::GLMapOccupancy(GLRenderer& renderer, const client::GameMap& map)
		    : device(renderer.GetGLDevice()), map(map) {
			SPADES_MARK_FUNCTION();

			const IntVector3 size = MakeIntVector3(map.Width(), map.Height(), map.Depth());
			isRegionDirty.resize((std::size_t)(size.x / kRegionSize) * (size.y / kRegionSize) *
			                     (size.z / kRegionSize));

			std::vector<std::uint8_t> texels;
			ReadBlocks(MakeIntVector3(0, 0, 0), size, texels);

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

		void GLMapOccupancy::ReadBlocks(const IntVector3& origin, const IntVector3& size,
		                                std::vector<std::uint8_t>& texels) const {
			texels.resize((std::size_t)size.x * size.y * size.z);

			for (int y = 0; y < size.y; y++) {
				for (int x = 0; x < size.x; x++) {
					const std::uint64_t column = map.GetSolidMap(origin.x + x, origin.y + y);
					for (int z = 0; z < size.z; z++) {
						const bool solid = ((column >> (origin.z + z)) & 1) != 0;
						texels[x + (y + z * (std::size_t)size.y) * size.x] =
						  solid ? kSolid : kEmpty;
					}
				}
			}
		}

		Vector3 GLMapOccupancy::GetSize() const {
			return MakeVector3((float)map.Width(), (float)map.Height(), (float)map.Depth());
		}

		void GLMapOccupancy::GameMapChanged(int x, int y, int z) {
			if (x < 0 || y < 0 || z < 0 || x >= map.Width() || y >= map.Height() ||
			    z >= map.Depth())
				return;

			const IntVector3 region =
			  MakeIntVector3(x / kRegionSize, y / kRegionSize, z / kRegionSize);
			const int index = RegionIndex(region);
			if (isRegionDirty[index])
				return;
			isRegionDirty[index] = true;
			dirtyRegions.push_back(region);
		}

		void GLMapOccupancy::Update() {
			if (dirtyRegions.empty())
				return;

			const IntVector3 size = MakeIntVector3(kRegionSize, kRegionSize, kRegionSize);
			std::vector<std::uint8_t> texels;

			device.BindTexture(IGLDevice::Texture3D, texture);
			for (const IntVector3& region : dirtyRegions) {
				const IntVector3 origin = region * kRegionSize;
				ReadBlocks(origin, size, texels);
				device.TexSubImage3D(IGLDevice::Texture3D, 0, origin.x, origin.y, origin.z, size.x,
				                     size.y, size.z, IGLDevice::Red, IGLDevice::UnsignedByte,
				                     texels.data());
				isRegionDirty[RegionIndex(region)] = false;
			}
			dirtyRegions.clear();
		}
	} // namespace draw
} // namespace spades
