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
		} // namespace

		GLMapOccupancy::GLMapOccupancy(GLRenderer& renderer, const client::GameMap& map)
		    : device(renderer.GetGLDevice()), map(map) {
			SPADES_MARK_FUNCTION();

			const int w = map.Width(), h = map.Height(), d = map.Depth();
			isDirty.resize((std::size_t)w * h * d);

			// Texels run along x, then y, then z, as the texture's do.
			std::vector<std::uint8_t> texels((std::size_t)w * h * d);
			for (int y = 0; y < h; y++) {
				for (int x = 0; x < w; x++) {
					const std::uint64_t column = map.GetSolidMap(x, y);
					for (int z = 0; z < d; z++)
						texels[BlockIndex(x, y, z)] = ((column >> z) & 1) ? kSolid : kEmpty;
				}
			}

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
			device.TexImage3D(IGLDevice::Texture3D, 0, IGLDevice::Red, w, h, d, 0, IGLDevice::Red,
			                  IGLDevice::UnsignedByte, texels.data());
		}

		GLMapOccupancy::~GLMapOccupancy() { device.DeleteTexture(texture); }

		int GLMapOccupancy::BlockIndex(int x, int y, int z) const {
			return x + (y + z * map.Height()) * map.Width();
		}

		Vector3 GLMapOccupancy::GetSize() const {
			return MakeVector3((float)map.Width(), (float)map.Height(), (float)map.Depth());
		}

		void GLMapOccupancy::GameMapChanged(int x, int y, int z) {
			if (x < 0 || y < 0 || z < 0 || x >= map.Width() || y >= map.Height() ||
			    z >= map.Depth())
				return;

			const int index = BlockIndex(x, y, z);
			if (isDirty[index])
				return;
			isDirty[index] = true;
			dirtyBlocks.push_back(MakeIntVector3(x, y, z));
		}

		void GLMapOccupancy::Update() {
			if (dirtyBlocks.empty())
				return;

			device.BindTexture(IGLDevice::Texture3D, texture);
			for (const IntVector3& block : dirtyBlocks) {
				const std::uint8_t texel = map.IsSolid(block.x, block.y, block.z) ? kSolid : kEmpty;
				device.TexSubImage3D(IGLDevice::Texture3D, 0, block.x, block.y, block.z, 1, 1, 1,
				                     IGLDevice::Red, IGLDevice::UnsignedByte, &texel);
				isDirty[BlockIndex(block.x, block.y, block.z)] = false;
			}
			dirtyBlocks.clear();
		}
	} // namespace draw
} // namespace spades
