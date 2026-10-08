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

			// The lights the batch names rows of
			GLDynamicLightTable& lightTable = renderer->GetDynamicLightTable();
			device.ActiveTexture(texStage + 2);
			device.BindTexture(IGLDevice::Texture2D, lightTable.GetTexture());
			device.ActiveTexture(texStage);

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
