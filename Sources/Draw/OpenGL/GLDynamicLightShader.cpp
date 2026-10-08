/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "GLDynamicLightShader.h"
#include "GLImage.h"
#include "GLMapOccupancy.h"
#include "GLProgramManager.h"
#include "GLRenderer.h"
#include <Core/Settings.h>

namespace spades {
	namespace draw {
		GLDynamicLightShader::GLDynamicLightShader()
		    : dynamicLightOrigin("dynamicLightOrigin"),
		      dynamicLightColor("dynamicLightColor"),
		      dynamicLightRadius("dynamicLightRadius"),
		      dynamicLightRadiusInversed("dynamicLightRadiusInversed"),
		      dynamicLightSpotMatrix("dynamicLightSpotMatrix"),
		      dynamicLightProjectionTexture("dynamicLightProjectionTexture"),
		      dynamicLightIsLinear("dynamicLightIsLinear"),
		      dynamicLightLinearDirection("dynamicLightLinearDirection"),
		      dynamicLightLinearLength("dynamicLightLinearLength"),
		      dynamicLightMapOccupancy("dynamicLightMapOccupancy"),
		      dynamicLightMapSizeInversed("dynamicLightMapSizeInversed"),
		      dynamicLightMapOcclusion("dynamicLightMapOcclusion") {
			lastRenderer = NULL;
		}

		GLDynamicLightShader::~GLDynamicLightShader() {}

		std::vector<GLShader*>
		GLDynamicLightShader::RegisterShader(spades::draw::GLProgramManager* r) {
			std::vector<GLShader*> shaders;

			shaders.push_back(r->RegisterShader("Shaders/OpenGL/DynamicLight/Common.fs"));
			shaders.push_back(r->RegisterShader("Shaders/OpenGL/DynamicLight/Common.vs"));
			shaders.push_back(r->RegisterShader("Shaders/OpenGL/DynamicLight/MapOcclusion.fs"));

			shaders.push_back(r->RegisterShader("Shaders/OpenGL/DynamicLight/MapNull.fs"));
			shaders.push_back(r->RegisterShader("Shaders/OpenGL/DynamicLight/MapNull.vs"));

			return shaders;
		}

		int GLDynamicLightShader::operator()(GLRenderer* renderer, spades::draw::GLProgram* program,
		                                     const GLDynamicLight& light, int texStage) {
			// TODO: Raw pointers are not unique!
			if (lastRenderer != renderer) {
				whiteImage = renderer->RegisterWhiteImage().Cast<GLImage>();
				lastRenderer = renderer;
			}

			const client::DynamicLightParam& param = light.GetParam();

			IGLDevice& device = renderer->GetGLDevice();
			dynamicLightOrigin(program);
			dynamicLightColor(program);
			dynamicLightRadius(program);
			dynamicLightRadiusInversed(program);
			dynamicLightSpotMatrix(program);
			dynamicLightProjectionTexture(program);
			dynamicLightIsLinear(program);
			dynamicLightLinearDirection(program);
			dynamicLightLinearLength(program);
			dynamicLightMapOccupancy(program);
			dynamicLightMapSizeInversed(program);
			dynamicLightMapOcclusion(program);

			dynamicLightOrigin.SetValue(param.origin.x, param.origin.y, param.origin.z);
			dynamicLightColor.SetValue(param.color.x, param.color.y, param.color.z);
			dynamicLightRadius.SetValue(param.radius);
			dynamicLightRadiusInversed.SetValue(1.f / param.radius);

			if (param.type == client::DynamicLightTypeSpotlight) {
				device.ActiveTexture(texStage);
				GLImage* image = static_cast<GLImage*>(param.image);
				image->Bind(IGLDevice::Texture2D);
				// The image must not repeat past the cone's edge.
				image->SetWrap(IGLDevice::ClampToEdge);
				dynamicLightProjectionTexture.SetValue(texStage);
				texStage++;

				dynamicLightSpotMatrix.SetValue(light.GetProjectionMatrix());

				dynamicLightIsLinear.SetValue(0);
			} else if (param.type == client::DynamicLightTypePoint ||
			           param.type == client::DynamicLightTypeLinear) {
				device.ActiveTexture(texStage);
				whiteImage->Bind(IGLDevice::Texture2D);
				dynamicLightProjectionTexture.SetValue(texStage);
				texStage++;

				// The shader samples from a white image. However, we still have to make sure
				// UV is in a valid range so the fragments are not discarded.
				dynamicLightSpotMatrix.SetValue(Matrix4::Translate(0.5F, 0.5F, 0.0F) *
				                                Matrix4::Scale(0.0F));

				if (param.type == client::DynamicLightTypeLinear) {
					// Convert two endpoints to one endpoint + direction + length.
					// `Vector3::Normalize` is no-op when the length is zero,
					// therefore the zero-length case is handled.
					Vector3 direction = param.point2 - param.origin;
					float length = direction.GetLength();
					direction = direction.Normalize();

					dynamicLightLinearDirection.SetValue(direction.x, direction.y, direction.z);
					dynamicLightLinearLength.SetValue(length);

					dynamicLightIsLinear.SetValue(1);
				} else {
					dynamicLightIsLinear.SetValue(0);
				}
			} else {
				SPUnreachable();
			}

			// The map hides the light from what is behind it, once it is loaded.
			device.ActiveTexture(texStage);
			if (GLMapOccupancy* occupancy = renderer->GetMapOccupancy()) {
				device.BindTexture(IGLDevice::Texture3D, occupancy->GetTexture());
				const Vector3 size = occupancy->GetSize();
				dynamicLightMapSizeInversed.SetValue(1.0F / size.x, 1.0F / size.y, 1.0F / size.z);
				dynamicLightMapOcclusion.SetValue(1.0F);
			} else {
				device.BindTexture(IGLDevice::Texture3D, 0);
				dynamicLightMapOcclusion.SetValue(0.0F);
			}
			dynamicLightMapOccupancy.SetValue(texStage);
			texStage++;

			device.ActiveTexture(texStage);

			return texStage;
		}
	} // namespace draw
} // namespace spades