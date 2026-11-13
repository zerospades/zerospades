/*
 Copyright (c) 2016 yvt

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

#include <cmath>
#include <vector>

#include "GLImage.h"
#include "GLProfiler.h"
#include "GLProgram.h"
#include "GLProgramAttribute.h"
#include "GLProgramUniform.h"
#include "GLQuadRenderer.h"
#include "GLRenderer.h"
#include "GLSSAOFilter.h"
#include "IGLDevice.h"
#include <Core/Debug.h>
#include <Core/Math.h>

namespace spades {
	namespace draw {

		GLSSAOFilter::GLSSAOFilter(GLRenderer& renderer)
		    : renderer(renderer), settings(renderer.GetSettings()) {
			ssaoProgram = renderer.RegisterProgram("Shaders/OpenGL/PostFilters/SSAO.program");
			bilateralProgram = renderer.RegisterProgram("Shaders/OpenGL/PostFilters/BilateralFilter.program");
			ditherPattern = renderer.RegisterImage("Gfx/DitherPattern4x4.png").Cast<GLImage>();
		}

		GLColorBuffer GLSSAOFilter::GenerateRawSSAOImage(int width, int height) {
			SPADES_MARK_FUNCTION();
			IGLDevice& dev = renderer.GetGLDevice();
			GLQuadRenderer qr(dev);

			GLColorBuffer output =
			  renderer.GetFramebufferManager()->CreateBufferHandle(width, height, 1);

			{
				GLProgram* program = ssaoProgram;
				static GLProgramAttribute positionAttribute("positionAttribute");
				static GLProgramUniform depthTexture("depthTexture");
				static GLProgramUniform ditherTexture("ditherTexture");
				static GLProgramUniform texCoordRange("texCoordRange");
				static GLProgramUniform zNearFar("zNearFar");
				static GLProgramUniform pixelShift("pixelShift");
				static GLProgramUniform fieldOfView("fieldOfView");
				static GLProgramUniform sampleOffsetScale("sampleOffsetScale");
				static GLProgramUniform perspective("perspective");

				positionAttribute(program);
				depthTexture(program);
				ditherTexture(program);
				texCoordRange(program);
				zNearFar(program);
				pixelShift(program);
				fieldOfView(program);
				sampleOffsetScale(program);
				perspective(program);

				program->Use();

				const client::SceneDefinition& def = renderer.GetSceneDef();

				zNearFar.SetValue(def.zNear, def.zFar);
				// How far across the view reaches: per unit of depth in a
				// perspective view, and outright in a parallel one, where the
				// shader must not scale it by the depth (nor read the depth
				// buffer as the reciprocal curve a frustum writes).
				if (def.orthogonal) {
					const float halfHeight = def.orthoHeight * 0.5F;
					const float halfWidth =
					  halfHeight * (std::tan(def.fovX * 0.5F) / std::tan(def.fovY * 0.5F));
					fieldOfView.SetValue(halfWidth, halfHeight);
				} else {
					fieldOfView.SetValue(std::tan(def.fovX * 0.5F), std::tan(def.fovY * 0.5F));
				}
				perspective.SetValue(def.orthogonal ? 0.0F : 1.0F);
				pixelShift.SetValue(1.0F / (float)width, 1.0F / (float)height);

				float kernelSize = std::max(1.0F, std::min(width, height) * 0.0018F);
				sampleOffsetScale.SetValue(kernelSize / (float)width, kernelSize / (float)height);

				if (width < renderer.GetRenderWidth()) {
					// 2x downsampling
					texCoordRange.SetValue(0.25F / width, 0.25F / height, 1.0F, 1.0F);
				} else {
					texCoordRange.SetValue(0.0F, 0.0F, 1.0F, 1.0F);
				}

				dev.ActiveTexture(0);
				depthTexture.SetValue(0);
				dev.BindTexture(IGLDevice::Texture2D,
				                renderer.GetFramebufferManager()->GetDepthTexture());

				dev.ActiveTexture(1);
				ditherTexture.SetValue(1);
				ditherPattern->Bind(IGLDevice::Texture2D);
				dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMagFilter,
				                 IGLDevice::Nearest);
				dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMinFilter,
				                 IGLDevice::Nearest);

				dev.BindFramebuffer(IGLDevice::Framebuffer, output.GetFramebuffer());
				dev.Viewport(0, 0, width, height);
				qr.SetCoordAttributeIndex(positionAttribute());
				qr.Draw();
				dev.ActiveTexture(0);
				dev.BindTexture(IGLDevice::Texture2D, 0);
			}

			return output;
		}

		GLColorBuffer GLSSAOFilter::ApplyBilateralFilter(GLColorBuffer tex, bool direction,
		                                                 int width, int height) {
			SPADES_MARK_FUNCTION();
			// do gaussian blur
			GLProgram* program = bilateralProgram;
			IGLDevice& dev = renderer.GetGLDevice();
			GLQuadRenderer qr(dev);

			int w = (width == -1) ? tex.GetWidth() : width;
			int h = (height == -1) ? tex.GetHeight() : height;

			GLColorBuffer buf2 = renderer.GetFramebufferManager()->CreateBufferHandle(w, h, 1);

			static GLProgramAttribute positionAttribute("positionAttribute");
			static GLProgramUniform inputTexture("inputTexture");
			static GLProgramUniform depthTexture("depthTexture");
			static GLProgramUniform texCoordRange("texCoordRange");
			static GLProgramUniform unitShift("unitShift");
			static GLProgramUniform zNearFar("zNearFar");
			static GLProgramUniform isUpsampling("isUpsampling");
			static GLProgramUniform pixelShift("pixelShift");
			program->Use();

			positionAttribute(program);
			inputTexture(program);
			depthTexture(program);
			texCoordRange(program);
			unitShift(program);
			zNearFar(program);
			isUpsampling(program);
			pixelShift(program);

			inputTexture.SetValue(0);
			dev.ActiveTexture(0);
			dev.BindTexture(IGLDevice::Texture2D, tex.GetTexture());
			dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMagFilter, IGLDevice::Nearest);
			dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMinFilter, IGLDevice::Nearest);

			depthTexture.SetValue(1);
			dev.ActiveTexture(1);
			dev.BindTexture(IGLDevice::Texture2D,
			                renderer.GetFramebufferManager()->GetDepthTexture());

			texCoordRange.SetValue(0.0F, 0.0F, 1.0F, 1.0F);

			unitShift.SetValue(direction ? 1.0F / (float)width : 0.0F,
			                   direction ? 0.0F : 1.0F / (float)height);

			pixelShift.SetValue(1.0F / (float)width, 1.0F / (float)height,
				(float)width, (float)height);

			isUpsampling.SetValue((width > tex.GetWidth()) ? 1 : 0);

			const client::SceneDefinition& def = renderer.GetSceneDef();
			zNearFar.SetValue(def.zNear, def.zFar);

			qr.SetCoordAttributeIndex(positionAttribute());
			dev.Viewport(0, 0, w, h);
			dev.BindFramebuffer(IGLDevice::Framebuffer, buf2.GetFramebuffer());
			qr.Draw();

			dev.ActiveTexture(0);
			dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMagFilter, IGLDevice::Linear);
			dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMinFilter, IGLDevice::Linear);

			return buf2;
		}

		GLColorBuffer GLSSAOFilter::Filter() {
			SPADES_MARK_FUNCTION();

			IGLDevice& dev = renderer.GetGLDevice();

			int width = renderer.GetRenderWidth();
			int height = renderer.GetRenderHeight();

			dev.Enable(IGLDevice::Blend, false);

			bool mirror = renderer.IsRenderingMirror();
			bool useLowQualitySSAO = mirror || renderer.GetSettings().r_ssao >= 2;
			GLColorBuffer ssao = useLowQualitySSAO
			                       ? GenerateRawSSAOImage((width + 1) / 2, (height + 1) / 2)
			                       : GenerateRawSSAOImage(width, height);
			ssao = ApplyBilateralFilter(ssao, false, width, height);
			ssao = ApplyBilateralFilter(ssao, true, width, height);
			if (!mirror) {
				ssao = ApplyBilateralFilter(ssao, false, width, height);
				ssao = ApplyBilateralFilter(ssao, true, width, height);
			}

			return ssao;
		}
	} // namespace draw
} // namespace spades