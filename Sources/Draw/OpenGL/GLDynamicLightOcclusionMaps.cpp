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

#include <cstring>

#include "GLDynamicLight.h"
#include "GLDynamicLightOcclusionMaps.h"
#include "GLDynamicLightTable.h"
#include "GLMapOccupancy.h"
#include "GLProgram.h"
#include "GLProgramAttribute.h"
#include "GLProgramUniform.h"
#include "GLQuadRenderer.h"
#include "GLRenderer.h"
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		namespace {
			/**
			 * The widest a texel may get at a light's reach, in blocks. Its width is
			 * how far off an edge of the beam's shadow can land.
			 */
			constexpr float kMaxTexelWidth = 1.5F;
		} // namespace

		float GLDynamicLightOcclusionMaps::GetTexelSpread(const GLDynamicLight& light) {
			return 2.0F * light.GetSpotTangent() * GLDynamicLight::SpotFadeEnd / (float)TileSize;
		}

		bool GLDynamicLightOcclusionMaps::IsMappable(const GLDynamicLight& light) {
			const client::DynamicLightParam& param = light.GetParam();
			if (param.type != client::DynamicLightTypeSpotlight)
				return false;
			return param.radius * GetTexelSpread(light) <= kMaxTexelWidth;
		}

		GLDynamicLightOcclusionMaps::GLDynamicLightOcclusionMaps(GLRenderer& renderer)
		    : renderer(renderer),
		      device(renderer.GetGLDevice()),
		      program(renderer.RegisterProgram("Shaders/OpenGL/DynamicLight/OcclusionMap.program")) {
			// A texel holds one distance: one channel, where the card can draw into
			// textures of fewer than four.
			const char* extensions = device.GetString(IGLDevice::Extensions);
			const bool oneChannel =
			  extensions && std::strstr(extensions, "GL_ARB_texture_rg") != nullptr;

			texture = device.GenTexture();
			device.BindTexture(IGLDevice::Texture2D, texture);
			device.TexImage2D(IGLDevice::Texture2D, 0,
			                  oneChannel ? IGLDevice::R16F : IGLDevice::RGBA16F, AtlasSize,
			                  AtlasSize, 0, oneChannel ? IGLDevice::Red : IGLDevice::RGBA,
			                  IGLDevice::FloatType, nullptr);
			// Looked up texel by texel: nothing to filter, and no mipmaps.
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMagFilter,
			                    IGLDevice::Nearest);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMinFilter,
			                    IGLDevice::Nearest);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureWrapS,
			                    IGLDevice::ClampToEdge);
			device.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureWrapT,
			                    IGLDevice::ClampToEdge);

			framebuffer = device.GenFramebuffer();
			device.BindFramebuffer(IGLDevice::Framebuffer, framebuffer);
			device.FramebufferTexture2D(IGLDevice::Framebuffer, IGLDevice::ColorAttachment0,
			                            IGLDevice::Texture2D, texture, 0);
			device.BindFramebuffer(IGLDevice::Framebuffer, 0);
			device.BindTexture(IGLDevice::Texture2D, 0);
		}

		GLDynamicLightOcclusionMaps::~GLDynamicLightOcclusionMaps() {
			device.DeleteFramebuffer(framebuffer);
			device.DeleteTexture(texture);
		}

		void GLDynamicLightOcclusionMaps::Render(const std::vector<GLDynamicLight>& lights,
		                                         const GLDynamicLightTable& table) {
			SPADES_MARK_FUNCTION();

			GLMapOccupancy* occupancy = renderer.GetMapOccupancy();
			if (!occupancy || table.GetNumTiles() == 0)
				return;

			static GLProgramAttribute positionAttribute("positionAttribute");
			static GLProgramUniform mapOccupancy("dynamicLightMapOccupancy");
			static GLProgramUniform mapSizeInversed("dynamicLightMapSizeInversed");
			static GLProgramUniform mapOcclusion("dynamicLightMapOcclusion");
			static GLProgramUniform lightOrigin("lightOrigin");
			static GLProgramUniform lightAxisX("lightAxisX");
			static GLProgramUniform lightAxisY("lightAxisY");
			static GLProgramUniform lightAxisZ("lightAxisZ");
			static GLProgramUniform lightSpread("lightSpread");
			static GLProgramUniform lightReach("lightReach");

			device.BindFramebuffer(IGLDevice::Framebuffer, framebuffer);
			device.Enable(IGLDevice::Blend, false);
			device.Enable(IGLDevice::DepthTest, false);
			device.Enable(IGLDevice::CullFace, false);

			program->Use();
			positionAttribute(program);

			device.ActiveTexture(0);
			device.BindTexture(IGLDevice::Texture3D, occupancy->GetTexture());
			mapOccupancy(program);
			mapOccupancy.SetValue(0);
			const Vector3 size = occupancy->GetSize();
			mapSizeInversed(program);
			mapSizeInversed.SetValue(1.F / size.x, 1.F / size.y, 1.F / size.z);
			mapOcclusion(program);
			mapOcclusion.SetValue(1.F);

			lightOrigin(program);
			lightAxisX(program);
			lightAxisY(program);
			lightAxisZ(program);
			lightSpread(program);
			lightReach(program);

			GLQuadRenderer qr(device);
			qr.SetCoordAttributeIndex(positionAttribute());

			for (std::size_t row = 0; row < lights.size(); row++) {
				const int tile = table.GetTile(row);
				if (tile < 0)
					continue;

				const GLDynamicLight& light = lights[row];
				const client::DynamicLightParam& param = light.GetParam();

				device.Viewport((tile % TilesPerRow) * TileSize, (tile / TilesPerRow) * TileSize,
				                TileSize, TileSize);

				const Vector3 x = param.spotAxis[0].Normalize();
				const Vector3 y = param.spotAxis[1].Normalize();
				const Vector3 z = param.spotAxis[2].Normalize();
				lightOrigin.SetValue(param.origin.x, param.origin.y, param.origin.z);
				lightAxisX.SetValue(x.x, x.y, x.z);
				lightAxisY.SetValue(y.x, y.y, y.z);
				lightAxisZ.SetValue(z.x, z.y, z.z);
				lightSpread.SetValue(GetTexelSpread(light) * (float)TileSize);
				lightReach.SetValue(param.radius);
				qr.Draw();
			}

			device.BindTexture(IGLDevice::Texture3D, 0);
			device.BindFramebuffer(IGLDevice::Framebuffer, 0);
		}
	} // namespace draw
} // namespace spades
