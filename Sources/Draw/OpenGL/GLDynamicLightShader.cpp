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

#include <string>

#include "GLImage.h"
#include "GLMapOccupancy.h"
#include "GLProgramManager.h"
#include "GLRenderer.h"
#include <Core/Settings.h>

namespace spades {
	namespace draw {
		namespace {
			std::string Element(const char* name, std::size_t index) {
				return std::string(name) + "[" + std::to_string(index) + "]";
			}
		} // namespace

		GLDynamicLightShader::LightUniforms::LightUniforms(std::size_t index)
		    : origin(Element("dynamicLightOrigin", index)),
		      color(Element("dynamicLightColor", index)),
		      radius(Element("dynamicLightRadius", index)),
		      radiusInversed(Element("dynamicLightRadiusInversed", index)),
		      spotMatrix(Element("dynamicLightSpotMatrix", index)),
		      isSpot(Element("dynamicLightIsSpot", index)),
		      isLinear(Element("dynamicLightIsLinear", index)),
		      linearDirection(Element("dynamicLightLinearDirection", index)),
		      linearLength(Element("dynamicLightLinearLength", index)) {}

		GLDynamicLightShader::GLDynamicLightShader()
		    : count("dynamicLightCount"),
		      projectionTexture("dynamicLightProjectionTexture"),
		      mapOccupancy("dynamicLightMapOccupancy"),
		      mapSizeInversed("dynamicLightMapSizeInversed"),
		      mapOcclusion("dynamicLightMapOcclusion"),
		      eye("dynamicLightEye") {
			lightUniforms.reserve(MaxLightsPerDraw);
			for (std::size_t i = 0; i < MaxLightsPerDraw; i++)
				lightUniforms.emplace_back(i);
		}

		GLDynamicLightShader::~GLDynamicLightShader() {}

		std::vector<GLShader*>
		GLDynamicLightShader::RegisterShader(spades::draw::GLProgramManager* r) {
			std::vector<GLShader*> shaders;

			shaders.push_back(r->RegisterShader("Shaders/OpenGL/DynamicLight/Lights.fs"));
			shaders.push_back(r->RegisterShader("Shaders/OpenGL/DynamicLight/Common.fs"));
			shaders.push_back(r->RegisterShader("Shaders/OpenGL/DynamicLight/Common.vs"));
			shaders.push_back(r->RegisterShader("Shaders/OpenGL/DynamicLight/MapOcclusion.fs"));

			shaders.push_back(r->RegisterShader("Shaders/OpenGL/DynamicLight/MapNull.fs"));
			shaders.push_back(r->RegisterShader("Shaders/OpenGL/DynamicLight/MapNull.vs"));

			return shaders;
		}

		GLImage* GLDynamicLightShader::GetSpotImage(const GLDynamicLight& light) {
			const client::DynamicLightParam& param = light.GetParam();
			if (param.type != client::DynamicLightTypeSpotlight)
				return nullptr;
			return static_cast<GLImage*>(param.image);
		}

		GLImage* GLDynamicLightShader::TakeBatch() {
			batch.clear();
			deferred.clear();

			GLImage* image = nullptr;
			for (const GLDynamicLight* light : pending) {
				GLImage* lightImage = GetSpotImage(*light);
				const bool fits = batch.size() < MaxLightsPerDraw &&
				                  (!lightImage || !image || lightImage == image);
				if (!fits) {
					deferred.push_back(light);
					continue;
				}
				if (lightImage)
					image = lightImage;
				batch.push_back(light);
			}
			return image;
		}

		void GLDynamicLightShader::SetUp(GLRenderer* renderer, GLProgram* program, GLImage* image,
		                                 int texStage) {
			// TODO: Raw pointers are not unique!
			if (lastRenderer != renderer) {
				whiteImage = renderer->RegisterWhiteImage().Cast<GLImage>();
				lastRenderer = renderer;
				// Its programs hold nothing uploaded yet.
				uploadedProgram = nullptr;
			}

			// Bound for every draw: other passes use the same texture stage.
			IGLDevice& device = renderer->GetGLDevice();
			device.ActiveTexture(texStage);
			if (image) {
				image->Bind(IGLDevice::Texture2D);
				// The image must not repeat past the cone's edge.
				image->SetWrap(IGLDevice::ClampToEdge);
			} else {
				whiteImage->Bind(IGLDevice::Texture2D);
			}

			// The map hides the lights from what is behind it, once it is loaded.
			GLMapOccupancy* occupancy = renderer->GetMapOccupancy();
			device.ActiveTexture(texStage + 1);
			device.BindTexture(IGLDevice::Texture3D, occupancy ? occupancy->GetTexture() : 0);
			device.ActiveTexture(texStage);

			const std::uint32_t frame = renderer->GetFrameNumber();
			if (program == uploadedProgram && frame == uploadedFrame && batch == uploadedBatch)
				return;
			uploadedProgram = program;
			uploadedFrame = frame;
			uploadedBatch = batch;

			projectionTexture(program);
			projectionTexture.SetValue(texStage);
			mapOccupancy(program);
			mapOccupancy.SetValue(texStage + 1);
			mapOcclusion(program);
			mapOcclusion.SetValue(occupancy ? 1.F : 0.F);
			if (occupancy) {
				const Vector3 size = occupancy->GetSize();
				mapSizeInversed(program);
				mapSizeInversed.SetValue(1.F / size.x, 1.F / size.y, 1.F / size.z);
			}
			// Where the first-person view's models are lit from, as far as the map
			// hiding a light from them goes
			const Vector3& viewOrigin = renderer->GetSceneDef().viewOrigin;
			eye(program);
			eye.SetValue(viewOrigin.x, viewOrigin.y, viewOrigin.z);
			count(program);
			count.SetValue(static_cast<IGLDevice::Integer>(batch.size()));

			for (std::size_t i = 0; i < batch.size(); i++) {
				const GLDynamicLight& light = *batch[i];
				const client::DynamicLightParam& param = light.GetParam();
				LightUniforms& u = lightUniforms[i];

				u.origin(program);
				u.color(program);
				u.radius(program);
				u.radiusInversed(program);
				u.spotMatrix(program);
				u.isSpot(program);
				u.isLinear(program);

				u.origin.SetValue(param.origin.x, param.origin.y, param.origin.z);
				u.color.SetValue(param.color.x, param.color.y, param.color.z);
				u.radius.SetValue(param.radius);
				u.radiusInversed.SetValue(1.F / param.radius);

				if (param.type == client::DynamicLightTypeSpotlight) {
					u.spotMatrix.SetValue(light.GetProjectionMatrix());
					u.isSpot.SetValue(1.F);
					u.isLinear.SetValue(0.F);
				} else if (param.type == client::DynamicLightTypePoint ||
				           param.type == client::DynamicLightTypeLinear) {
					// Maps everything to the image's centre, so the shader can sample it.
					u.spotMatrix.SetValue(Matrix4::Translate(0.5F, 0.5F, 0.0F) *
					                      Matrix4::Scale(0.0F));
					u.isSpot.SetValue(0.F);

					if (param.type == client::DynamicLightTypeLinear) {
						// Convert two endpoints to one endpoint + direction + length.
						// `Vector3::Normalize` is no-op when the length is zero,
						// therefore the zero-length case is handled.
						Vector3 direction = param.point2 - param.origin;
						float length = direction.GetLength();
						direction = direction.Normalize();

						u.linearDirection(program);
						u.linearLength(program);
						u.linearDirection.SetValue(direction.x, direction.y, direction.z);
						u.linearLength.SetValue(length);
						u.isLinear.SetValue(1.F);
					} else {
						u.isLinear.SetValue(0.F);
					}
				} else {
					SPUnreachable();
				}
			}
		}
	} // namespace draw
} // namespace spades
