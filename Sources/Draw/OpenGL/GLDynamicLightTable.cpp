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

#include "GLDynamicLight.h"
#include "GLDynamicLightTable.h"
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		namespace {
			/** The fewest rows the texture is made with, so few frames have to grow it. */
			constexpr int kMinCapacity = 64;
		} // namespace

		GLDynamicLightTable::GLDynamicLightTable(IGLDevice& device)
		    : device(device), texture(device.GenTexture()) {
			device.BindTexture(IGLDevice::Texture2D, texture);
			// Looked up texel by texel: nothing to filter, and no mipmaps.
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMagFilter,
			                    IGLDevice::Nearest);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMinFilter,
			                    IGLDevice::Nearest);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureWrapS,
			                    IGLDevice::ClampToEdge);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureWrapT,
			                    IGLDevice::ClampToEdge);
		}

		GLDynamicLightTable::~GLDynamicLightTable() { device.DeleteTexture(texture); }

		void GLDynamicLightTable::Update(const std::vector<GLDynamicLight>& newLights) {
			SPADES_MARK_FUNCTION();

			lights = newLights.data();
			numLights = newLights.size();
			if (numLights == 0)
				return; // nothing will look the table up

			device.BindTexture(IGLDevice::Texture2D, texture);

			if ((int)numLights > capacity) {
				capacity = std::max(capacity, kMinCapacity);
				while (capacity < (int)numLights)
					capacity *= 2;
				device.TexImage2D(IGLDevice::Texture2D, 0, IGLDevice::RGBA32F, TexelsPerLight,
				                  capacity, 0, IGLDevice::RGBA, IGLDevice::FloatType, nullptr);
			}

			texels.assign(numLights * TexelsPerLight * 4, 0.0F);
			for (std::size_t i = 0; i < numLights; i++) {
				const GLDynamicLight& light = newLights[i];
				const client::DynamicLightParam& param = light.GetParam();
				float* row = &texels[i * TexelsPerLight * 4];

				// 0: origin, reach
				row[0] = param.origin.x;
				row[1] = param.origin.y;
				row[2] = param.origin.z;
				row[3] = param.radius;

				// 1: colour, 1 / reach
				row[4] = param.color.x;
				row[5] = param.color.y;
				row[6] = param.color.z;
				row[7] = 1.0F / param.radius;

				// 2 to 5: the matrix projecting onto the light's image, by column. A
				// point or linear light's maps everything to its centre, so that it
				// can still be sampled.
				const bool spot = param.type == client::DynamicLightTypeSpotlight;
				const Matrix4 projection =
				  spot ? light.GetProjectionMatrix()
				       : Matrix4::Translate(0.5F, 0.5F, 0.0F) * Matrix4::Scale(0.0F);
				std::copy(projection.m, projection.m + 16, row + 8);

				// 6: a linear light's direction and length
				const bool linear = param.type == client::DynamicLightTypeLinear;
				if (linear) {
					// `Vector3::Normalize` is no-op when the length is zero, therefore
					// the zero-length case is handled.
					const Vector3 segment = param.point2 - param.origin;
					const Vector3 direction = segment.Normalize();
					row[24] = direction.x;
					row[25] = direction.y;
					row[26] = direction.z;
					row[27] = segment.GetLength();
				}

				// 7: whether it is a spotlight, and whether it is linear
				row[28] = spot ? 1.0F : 0.0F;
				row[29] = linear ? 1.0F : 0.0F;
			}

			device.TexSubImage2D(IGLDevice::Texture2D, 0, 0, 0, TexelsPerLight, (int)numLights,
			                     IGLDevice::RGBA, IGLDevice::FloatType, texels.data());
		}

		int GLDynamicLightTable::GetRow(const GLDynamicLight& light) const {
			const std::size_t row = light.GetTableRow();
			SPAssert(row < numLights && &lights[row] == &light);
			return (int)row;
		}
	} // namespace draw
} // namespace spades
