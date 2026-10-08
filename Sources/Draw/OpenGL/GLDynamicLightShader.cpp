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

#include <algorithm>

#include "GLDynamicLightOcclusionMaps.h"
#include "GLDynamicLightTable.h"
#include "GLImage.h"
#include "GLMapOccupancy.h"
#include "GLProgramManager.h"
#include "GLRenderer.h"
#include <Core/Settings.h>

namespace spades {
	namespace draw {
		GLDynamicLightShader::GLDynamicLightShader()
		    : projectionTexture("dynamicLightProjectionTexture"),
		      mapOccupancy("dynamicLightMapOccupancy"),
		      mapSizeInversed("dynamicLightMapSizeInversed"),
		      mapOcclusion("dynamicLightMapOcclusion"),
		      eye("dynamicLightEye"),
		      table("dynamicLightTable"),
		      tableRowsInversed("dynamicLightTableRowsInversed"),
		      occlusionMaps("dynamicLightOcclusionMaps"),
		      count("dynamicLightCount"),
		      rowsLow("dynamicLightRows[0]"),
		      rowsHigh("dynamicLightRows[1]") {}

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
			// A new renderer has its own images and programs, none of them set up.
			if (lastRenderer != renderer->GetInstanceId()) {
				whiteImage = renderer->RegisterImage("Gfx/White.tga").Cast<GLImage>();
				lastRenderer = renderer->GetInstanceId();
				// Its programs hold nothing uploaded yet, and it binds nothing yet.
				uploadedProgram = nullptr;
				boundPass = 0;
			}

			IGLDevice& device = renderer->GetGLDevice();
			GLMapOccupancy* occupancy = renderer->GetMapOccupancy();
			GLDynamicLightTable& lightTable = renderer->GetDynamicLightTable();

			// Once a pass, the textures every batch shares
			const std::uint32_t pass = renderer->GetDynamicLightPass();
			if (pass != boundPass) {
				boundPass = pass;
				boundImage = nullptr;

				// The map hides the lights from what is behind it, once it is loaded.
				device.ActiveTexture(texStage + 1);
				device.BindTexture(IGLDevice::Texture3D, occupancy ? occupancy->GetTexture() : 0);

				// The lights the batch names rows of
				device.ActiveTexture(texStage + 2);
				device.BindTexture(IGLDevice::Texture2D, lightTable.GetTexture());

				// Where the map stops the spotlights that have a map
				device.ActiveTexture(texStage + 3);
				device.BindTexture(IGLDevice::Texture2D,
				                   renderer->GetDynamicLightOcclusionMaps().GetTexture());
			}

			// The batch's image, when it is another one than the last batch's
			device.ActiveTexture(texStage);
			GLImage* batchImage = image ? image : whiteImage.GetPointerOrNull();
			if (batchImage != boundImage) {
				boundImage = batchImage;
				batchImage->Bind(IGLDevice::Texture2D);
				if (image) {
					// The image must not repeat past the cone's edge, and it is
					// sampled where only some fragments of a quad reach the light,
					// where a mipmap level cannot be chosen.
					image->SetWrap(IGLDevice::ClampToEdge);
					image->SetMinFilter(IGLDevice::Linear);
				}
			}

			const std::uint32_t frame = renderer->GetFrameNumber();
			if (program != uploadedProgram || frame != uploadedFrame) {
				uploadedProgram = program;
				uploadedFrame = frame;
				uploadedBatch.clear();

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
				table(program);
				table.SetValue(texStage + 2);
				tableRowsInversed(program);
				tableRowsInversed.SetValue(1.F / (float)std::max(lightTable.GetCapacity(), 1));
				occlusionMaps(program);
				occlusionMaps.SetValue(texStage + 3);
			}

			if (batch == uploadedBatch)
				return;
			uploadedBatch = batch;

			float rows[MaxLightsPerDraw] = {};
			for (std::size_t i = 0; i < batch.size(); i++)
				rows[i] = (float)lightTable.GetRow(*batch[i]);

			static_assert(MaxLightsPerDraw == 8, "the rows are sent as two vec4s");
			count(program);
			count.SetValue(static_cast<IGLDevice::Integer>(batch.size()));
			rowsLow(program);
			rowsLow.SetValue(rows[0], rows[1], rows[2], rows[3]);
			rowsHigh(program);
			rowsHigh.SetValue(rows[4], rows[5], rows[6], rows[7]);
		}
	} // namespace draw
} // namespace spades
