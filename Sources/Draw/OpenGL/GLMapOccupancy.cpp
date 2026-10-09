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
#include <vector>

#include "GLMapOccupancy.h"
#include "GLRenderer.h"
#include <Client/GameMap.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		GLMapOccupancy::GLMapOccupancy(GLRenderer& renderer, const client::GameMap& map)
		    : device(renderer.GetGLDevice()), clearance(map) {
			SPADES_MARK_FUNCTION();

			const IntVector3 size = clearance.GetSize();
			std::vector<std::uint8_t> texels;
			clearance.ComputeAll(texels);

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

		Vector3 GLMapOccupancy::GetSize() const {
			const IntVector3 size = clearance.GetSize();
			return MakeVector3((float)size.x, (float)size.y, (float)size.z);
		}

		void GLMapOccupancy::GameMapChanged(int x, int y, int z) {
			clearance.GameMapChanged(x, y, z);
		}

		void GLMapOccupancy::Update() {
			if (!clearance.HasChangedRegions())
				return;

			// Region rows are whole multiples of the default unpack alignment.
			static_assert(client::MapClearance::RegionSize % 4 == 0,
			              "region rows must be 4-byte aligned");

			const int size = client::MapClearance::RegionSize;
			device.BindTexture(IGLDevice::Texture3D, texture);
			clearance.TakeChangedRegions(
			  [&](const IntVector3& origin, const std::uint8_t* texels) {
				  device.TexSubImage3D(IGLDevice::Texture3D, 0, origin.x, origin.y, origin.z, size,
				                       size, size, IGLDevice::Red, IGLDevice::UnsignedByte, texels);
				  return true;
			  });
		}
	} // namespace draw
} // namespace spades
