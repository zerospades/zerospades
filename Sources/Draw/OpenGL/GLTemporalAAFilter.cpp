/*
 Copyright (c) 2017 yvt

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

#include <vector>

#include "GLFXAAFilter.h"
#include "GLProfiler.h"
#include "GLProgram.h"
#include "GLProgramAttribute.h"
#include "GLProgramUniform.h"
#include "GLQuadRenderer.h"
#include "GLRenderer.h"
#include "GLTemporalAAFilter.h"
#include "IGLDevice.h"
#include <Core/Debug.h>
#include <Core/Math.h>

namespace spades {
	namespace draw {
		GLTemporalAAFilter::GLTemporalAAFilter(GLRenderer& renderer) : renderer(renderer) {
			prevMatrix = Matrix4::Identity();
			prevViewOrigin = Vector3(0.0F, 0.0F, 0.0F);
			program = renderer.RegisterProgram("Shaders/OpenGL/PostFilters/TemporalAA.program");

			// Preload
			GLFXAAFilter{renderer};
		}

		GLTemporalAAFilter::~GLTemporalAAFilter() { DeleteHistoryBuffer(); }

		void GLTemporalAAFilter::DeleteHistoryBuffer() {
			if (!historyBuffer.valid)
				return;

			IGLDevice& dev = renderer.GetGLDevice();
			dev.DeleteFramebuffer(historyBuffer.framebuffer);
			dev.DeleteTexture(historyBuffer.texture);

			historyBuffer.valid = false;
		}

		GLColorBuffer GLTemporalAAFilter::Filter(GLColorBuffer input, bool useFxaa) {
			SPADES_MARK_FUNCTION();

			IGLDevice& dev = renderer.GetGLDevice();
			GLQuadRenderer qr(dev);

			// Calculate the current view-projection matrix.
			const client::SceneDefinition& def = renderer.GetSceneDef();
			
			Matrix4 viewMatrix = def.ToViewMatrix();
			Matrix4 projMatrix = def.ToOpenGLProjectionMatrix();

			// exclude translation from view matrix
			viewMatrix.m[12] = 0.0F;
			viewMatrix.m[13] = 0.0F;
			viewMatrix.m[14] = 0.0F;

			Matrix4 newMatrix = projMatrix * viewMatrix;

			// In `y = newMatrix * x`, the coordinate space `y` belongs to must
			// cover the clip region by range `[0, 1]` (like texture coordinates)
			// instead of `[-1, 1]` (like OpenGL clip coordinates)
			newMatrix = Matrix4::Translate(1.0F, 1.0F, 1.0F) * newMatrix;
			newMatrix = Matrix4::Scale(0.5F, 0.5F, 0.5F) * newMatrix;

			// Camera translation must be incorporated into the calculation
			// separately to avoid numerical errors. (You'd be suprised to see
			// how visible the visual artifacts can be.)
			Matrix4 translationMatrix = Matrix4::Translate(def.viewOrigin - prevViewOrigin);

			// Compute the reprojection matrix
			Matrix4 inverseNewMatrix = newMatrix.Inversed();
			Matrix4 diffMatrix = prevMatrix * translationMatrix * inverseNewMatrix;
			prevMatrix = newMatrix;
			prevViewOrigin = def.viewOrigin;

			if (!historyBuffer.valid || historyBuffer.width != input.GetWidth() ||
			    historyBuffer.height != input.GetHeight()) {
				DeleteHistoryBuffer();

				historyBuffer.width = input.GetWidth();
				historyBuffer.height = input.GetHeight();

				auto internalFormat = renderer.GetFramebufferManager()->GetMainInternalFormat();

				historyBuffer.framebuffer = dev.GenFramebuffer();
				dev.BindFramebuffer(IGLDevice::Framebuffer, historyBuffer.framebuffer);

				historyBuffer.texture = dev.GenTexture();
				dev.BindTexture(IGLDevice::Texture2D, historyBuffer.texture);

				historyBuffer.valid = true;

				SPLog("Creating a history buffer");
				dev.TexImage2D(IGLDevice::Texture2D, 0, internalFormat, historyBuffer.width,
				               historyBuffer.height, 0, IGLDevice::RGBA, IGLDevice::UnsignedByte,
				               NULL);

				SPLog("History buffer allocated");
				dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMagFilter, IGLDevice::Linear);
				dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMinFilter, IGLDevice::Linear);
				dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureWrapS, IGLDevice::ClampToEdge);
				dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureWrapT, IGLDevice::ClampToEdge);

				dev.FramebufferTexture2D(IGLDevice::Framebuffer, IGLDevice::ColorAttachment0,
				                         IGLDevice::Texture2D, historyBuffer.texture, 0);

				IGLDevice::Enum status = dev.CheckFramebufferStatus(IGLDevice::Framebuffer);
				if (status != IGLDevice::FramebufferComplete)
					SPRaise("Failed to create a history buffer.");
				SPLog("Created a history framebuffer");

				// Initialize the history buffer with the latest input
				dev.BindFramebuffer(IGLDevice::DrawFramebuffer, historyBuffer.framebuffer);
				dev.BindFramebuffer(IGLDevice::ReadFramebuffer, input.GetFramebuffer());
				dev.BlitFramebuffer(0, 0, input.GetWidth(), input.GetHeight(), 0, 0,
				                    input.GetWidth(), input.GetHeight(), IGLDevice::ColorBufferBit,
				                    IGLDevice::Nearest);
				dev.BindFramebuffer(IGLDevice::ReadFramebuffer, 0);
				dev.BindFramebuffer(IGLDevice::DrawFramebuffer, 0);

				// Reset the blending factor
				dev.BindFramebuffer(IGLDevice::Framebuffer, historyBuffer.framebuffer);
				dev.ColorMask(false, false, false, true);
				dev.ClearColor(0.0F, 0.0F, 0.0F, 0.5F);
				dev.Clear(IGLDevice::ColorBufferBit);
				dev.ColorMask(true, true, true, true);

				if (useFxaa)
					return GLFXAAFilter{renderer}.Filter(input);

				return input;
			}

			GLColorBuffer output = input.GetManager()->CreateBufferHandle();
			GLColorBuffer processedInput = input;

			static GLProgramAttribute positionAttribute("positionAttribute");
			static GLProgramUniform inputTexture("inputTexture");
			static GLProgramUniform depthTexture("depthTexture");
			static GLProgramUniform previousTexture("previousTexture");
			static GLProgramUniform processedInputTexture("processedInputTexture");
			static GLProgramUniform reprojectionMatrix("reprojectionMatrix");
			static GLProgramUniform inverseVP("inverseVP");
			static GLProgramUniform viewProjectionMatrixInv("viewProjectionMatrixInv");
			static GLProgramUniform fogDistance("fogDistance");

			dev.Enable(IGLDevice::Blend, false);

			positionAttribute(program);
			inputTexture(program);
			depthTexture(program);
			previousTexture(program);
			processedInputTexture(program);
			reprojectionMatrix(program);
			inverseVP(program);
			viewProjectionMatrixInv(program);
			fogDistance(program);

			program->Use();

			inputTexture.SetValue(0);
			previousTexture.SetValue(1);
			processedInputTexture.SetValue(2);
			depthTexture.SetValue(3);
			reprojectionMatrix.SetValue(diffMatrix);
			inverseVP.SetValue(1.0F / input.GetWidth(), 1.0F / input.GetHeight());
			viewProjectionMatrixInv.SetValue(inverseNewMatrix);
			fogDistance.SetValue(128.f);

			// Perform temporal AA
			// TODO: pre/post tone mapping to prevent aliasing near overbright area
			qr.SetCoordAttributeIndex(positionAttribute());
			dev.ActiveTexture(0);
			dev.BindTexture(IGLDevice::Texture2D, input.GetTexture());
			dev.ActiveTexture(1);
			dev.BindTexture(IGLDevice::Texture2D, historyBuffer.texture);
			dev.ActiveTexture(2);
			dev.BindTexture(IGLDevice::Texture2D, processedInput.GetTexture());
			dev.ActiveTexture(3);
			dev.BindTexture(IGLDevice::Texture2D,
			                renderer.GetFramebufferManager()->GetDepthTexture());
			dev.ActiveTexture(0);
			dev.BindFramebuffer(IGLDevice::Framebuffer, output.GetFramebuffer());
			dev.Viewport(0, 0, output.GetWidth(), output.GetHeight());
			qr.Draw();
			dev.BindTexture(IGLDevice::Texture2D, 0);

			// Copy the result to the history buffer
			dev.BindFramebuffer(IGLDevice::DrawFramebuffer, historyBuffer.framebuffer);
			dev.BindFramebuffer(IGLDevice::ReadFramebuffer, output.GetFramebuffer());
			dev.BlitFramebuffer(0, 0, input.GetWidth(), input.GetHeight(), 0, 0, input.GetWidth(),
			                    input.GetHeight(), IGLDevice::ColorBufferBit, IGLDevice::Nearest);
			dev.BindFramebuffer(IGLDevice::ReadFramebuffer, 0);
			dev.BindFramebuffer(IGLDevice::DrawFramebuffer, 0);
			return output;
		}
	} // namespace draw
} // namespace spades