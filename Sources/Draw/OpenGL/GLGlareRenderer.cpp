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

#include "GLFramebufferManager.h"
#include "GLGlareRenderer.h"
#include "GLImage.h"
#include "GLProgram.h"
#include "GLProgramAttribute.h"
#include "GLProgramUniform.h"
#include "GLQuadRenderer.h"
#include "GLRenderer.h"
#include "IGLDevice.h"
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		GLGlareRenderer::GLGlareRenderer(GLRenderer& renderer)
		    : renderer(renderer),
		      device(renderer.GetGLDevice()),
		      program(renderer.RegisterProgram("Shaders/OpenGL/Glare.program")) {}

		void GLGlareRenderer::Clear() { glares.clear(); }

		void GLGlareRenderer::Add(GLImage& image, const client::GlareParam& param) {
			Glare glare;
			glare.image = image;
			glare.param = param;
			glares.push_back(std::move(glare));
		}

		void GLGlareRenderer::Render() {
			SPADES_MARK_FUNCTION();

			if (glares.empty())
				return;

			const client::SceneDefinition& def = renderer.GetSceneDef();
			const Matrix4& projectionView = renderer.GetProjectionViewMatrix();

			// From the 2D units the glares are sized in to normalized device ones
			const float ndcPerUnitX = 2.0F / renderer.ScreenWidth();
			const float ndcPerUnitY = 2.0F / renderer.ScreenHeight();

			static GLProgramAttribute positionAttribute("positionAttribute");
			static GLProgramUniform drawRange("drawRange");
			static GLProgramUniform glareTexture("glareTexture");
			static GLProgramUniform glareColor("glareColor");
			static GLProgramUniform depthTexture("depthTexture");
			static GLProgramUniform firstPersonDepthEnd("firstPersonDepthEnd");

			program->Use();
			positionAttribute(program);
			drawRange(program);
			glareTexture(program);
			glareColor(program);
			depthTexture(program);
			firstPersonDepthEnd(program);

			device.ActiveTexture(1);
			device.BindTexture(IGLDevice::Texture2D,
			                   renderer.GetFramebufferManager()->GetDepthTexture());
			depthTexture.SetValue(1);
			device.ActiveTexture(0);
			glareTexture.SetValue(0);

			firstPersonDepthEnd.SetValue(GLRenderer::kFirstPersonDepthEnd);

			// Added onto the frame: what is underneath stays.
			device.Enable(IGLDevice::Blend, true);
			device.BlendFunc(IGLDevice::One, IGLDevice::One, IGLDevice::Zero, IGLDevice::One);

			GLQuadRenderer qr(device);
			qr.SetCoordAttributeIndex(positionAttribute());

			for (const Glare& glare : glares) {
				const client::GlareParam& param = glare.param;

				// `w` is how far ahead of the camera the light is.
				const Vector4 clip = projectionView * param.origin;
				if (clip.w <= def.zNear)
					continue;
				const float centreX = clip.x / clip.w;
				const float centreY = clip.y / clip.w;

				const float halfWidth = param.radius * ndcPerUnitX;
				const float halfHeight = param.radius * ndcPerUnitY;
				drawRange.SetValue(centreX - halfWidth, centreY - halfHeight, centreX + halfWidth,
				                   centreY + halfHeight);

				glareColor.SetValue(param.color.x, param.color.y, param.color.z);
				glare.image->Bind(IGLDevice::Texture2D);
				qr.Draw();
			}

			// Back to the blending the 2D drawing that follows uses
			device.BlendFunc(IGLDevice::One, IGLDevice::OneMinusSrcAlpha, IGLDevice::Zero,
			                 IGLDevice::One);
		}
	} // namespace draw
} // namespace spades
