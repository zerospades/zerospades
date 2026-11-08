/*
 Copyright (c) 2021 yvt

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

#include <array>
#include <vector>

#include "GLAmbientShadowRenderer.h"
#include "GLFogFilter2.h"
#include "GLImage.h"
#include "GLMapShadowRenderer.h"
#include "GLProgram.h"
#include "GLProgramAttribute.h"
#include "GLProgramUniform.h"
#include "GLQuadRenderer.h"
#include "GLRadiosityRenderer.h"
#include "GLRenderer.h"
#include "IGLDevice.h"
#include <Core/Debug.h>
#include <Core/Math.h>

namespace {
	constexpr int NoiseTexSize = 128;
}

namespace spades {
	namespace draw {
		GLFogFilter2::GLFogFilter2(GLRenderer& renderer) : renderer(renderer) {
			lens = renderer.RegisterProgram("Shaders/OpenGL/PostFilters/Fog2.program");
			ditherPattern = renderer.RegisterImage("Gfx/DitherPattern4x4.png").Cast<GLImage>();

			IGLDevice& dev = renderer.GetGLDevice();
			noiseTex = dev.GenTexture();
			dev.BindTexture(IGLDevice::Texture2D, noiseTex);
			dev.TexImage2D(IGLDevice::Texture2D, 0, IGLDevice::RGBA8, NoiseTexSize, NoiseTexSize, 0,
			               IGLDevice::BGRA, IGLDevice::UnsignedByte, NULL);
			dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMagFilter, IGLDevice::Nearest);
			dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMinFilter, IGLDevice::Nearest);
			dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureWrapS, IGLDevice::Repeat);
			dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureWrapT, IGLDevice::Repeat);
		}

		GLFogFilter2::~GLFogFilter2() { renderer.GetGLDevice().DeleteTexture(noiseTex); }

		GLColorBuffer GLFogFilter2::Filter(GLColorBuffer input) {
			SPADES_MARK_FUNCTION();

			IGLDevice& dev = renderer.GetGLDevice();
			GLAmbientShadowRenderer* ambientShadowRenderer = renderer.GetAmbientShadowRenderer();
			GLRadiosityRenderer* radiosityRenderer = renderer.GetRadiosityRenderer();
			GLQuadRenderer qr(dev);

			SPAssert(ambientShadowRenderer);
			SPAssert(radiosityRenderer);

			// Update `noiseTex` if the time has moved forward since the last time
			if (renderer.GetFrameNumber() != lastNoiseTexFrameNumber) {
				std::array<uint32_t, NoiseTexSize * NoiseTexSize> noise;

				for (uint32_t& x : noise)
					x = static_cast<std::uint32_t>(SampleRandom());

				dev.BindTexture(IGLDevice::Texture2D, noiseTex);
				dev.TexSubImage2D(IGLDevice::Texture2D, 0, 0, 0, NoiseTexSize, NoiseTexSize,
				                  IGLDevice::BGRA, IGLDevice::UnsignedByte, noise.data());

				lastNoiseTexFrameNumber = renderer.GetFrameNumber();
			}

			// Calculate the current view-projection matrix. Exclude `def.viewOrigin` from this matrix.
			client::SceneDefinition def = renderer.GetSceneDef();
			if (renderer.IsRenderingMirror()) {
				def.viewOrigin.z = 63.0F * 2.0F - def.viewOrigin.z;
				def.viewAxis[0].z = -def.viewAxis[0].z;
				def.viewAxis[1].z = -def.viewAxis[1].z;
				def.viewAxis[2].z = -def.viewAxis[2].z;
			}

			Matrix4 viewMatrix = def.ToViewMatrix();
			Matrix4 projMatrix = def.ToOpenGLProjectionMatrix();

			// exclude translation from view matrix
			viewMatrix.m[12] = 0.0F;
			viewMatrix.m[13] = 0.0F;
			viewMatrix.m[14] = 0.0F;

			Matrix4 viewProjectionMatrix = projMatrix * viewMatrix;

			// In `y = viewProjectionMatrix * x`, the coordinate space `y` belongs to must
			// cover the clip region by range `[0, 1]` (like texture coordinates)
			// instead of `[-1, 1]` (like OpenGL clip coordinates)
			viewProjectionMatrix = Matrix4::Translate(1.0F, 1.0F, 1.0F) * viewProjectionMatrix;
			viewProjectionMatrix = Matrix4::Scale(0.5F, 0.5F, 0.5F) * viewProjectionMatrix;

			// TODO: These were clearly copied from `GLLensFilter.cpp`. Remove the
			//       `lens` prefix!
			static GLProgramAttribute lensPosition("positionAttribute");
			static GLProgramUniform lensShadowMapTexture("shadowMapTexture");
			static GLProgramUniform ambientShadowTexture("ambientShadowTexture");
			static GLProgramUniform radiosityTexture("radiosityTexture");
			static GLProgramUniform lensColorTexture("colorTexture");
			static GLProgramUniform lensDepthTexture("depthTexture");
			static GLProgramUniform lensViewOrigin("viewOrigin");
			static GLProgramUniform viewProjectionMatrixInv("viewProjectionMatrixInv");
			static GLProgramUniform sunlightScale("sunlightScale");
			static GLProgramUniform ambientScale("ambientScale");
			static GLProgramUniform radiosityScale("radiosityScale");
			static GLProgramUniform sunlight("sunlight");
			static GLProgramUniform fogDistance("fogDistance");
			static GLProgramUniform ditherTexture("ditherTexture");
			static GLProgramUniform ditherOffset("ditherOffset");
			static GLProgramUniform noiseTexture("noiseTexture");

			dev.Enable(IGLDevice::Blend, false);

			lensPosition(lens);
			lensShadowMapTexture(lens);
			lensColorTexture(lens);
			lensDepthTexture(lens);
			lensViewOrigin(lens);
			sunlightScale(lens);
			sunlight(lens);
			ambientScale(lens);
			radiosityScale(lens);
			fogDistance(lens);
			ditherTexture(lens);
			ditherOffset(lens);
			viewProjectionMatrixInv(lens);
			ambientShadowTexture(lens);
			radiosityTexture(lens);
			noiseTexture(lens);

			lens->Use();

			lensViewOrigin.SetValue(def.viewOrigin.x, def.viewOrigin.y, def.viewOrigin.z);
			viewProjectionMatrixInv.SetValue(viewProjectionMatrix.Inversed());

			// Derived in full daylight, then scaled by the daylight below, so the fog
			// looks like the Fog Colour times the daylight.
			Vector3 fogCol = renderer.GetFullDaylightFogColor();
			fogCol *= fogCol; // linearize
			const float daylight = renderer.GetDaylight();
			sunlight.SetValue(renderer.GetSunlight());

			// Without the sun, the sky alone gives the fog its colour.
			const bool sunUp = renderer.GetSunlight() > 0.0F;
			float sunlightBrightness = sunUp ? 0.6F : 0.0F; // Sun -> Fog -> Eye
			float ambientBrightness = 1.0F;  // Sun -> Fog -> Fog -> Eye
			float radiosityBrightness = 1.0F;
			float radiosityOffset = 0.2F;
			// Let's say the fog modulates the incoming light with some factor
			// we call `fogTransmission`. When there are no occluding objects,
			// we see this color:
			//
			//     fogTransmission * sunlightBrightness +
			//         ambientBrightness * fogTransmission * fogColor
			//
			// This expresion's value must be equal to a given `fogColor`:
			//
			//     fogTransmission * sunlightBrightness +
			//       ambientBrightness * fogTransmission * fogColor = fogColor
			//
			// Solving this for `fogTransmission`:
			//
			//     fogTransmission = fogColor /
			// 			(sunlightBrightness + ambientBrightness * fogColor)
			//
			// We add some value (`radiosityOffset`) to `radiosityScale` for
			// artistic reasons. We want the fog to reflect some light.
			//
			// Without the sun this reduces to `1 / ambientBrightness`, which is taken
			// as is: a black channel of the Fog Colour would make it `0 / 0`.
			auto fogTransmission1 = [=](float fogColor) {
				if (!sunUp)
					return 1.0F / ambientBrightness;
				return fogColor / (sunlightBrightness + ambientBrightness * fogColor);
			};
			Vector3 fogTransmission{
			  fogTransmission1(fogCol.x),
			  fogTransmission1(fogCol.y),
			  fogTransmission1(fogCol.z),
			};
			// The radiosity is the sun's light the map bounces.
			Vector3 sunScale = fogTransmission * sunlightBrightness * renderer.GetSunlight();
			Vector3 ambient = fogTransmission * fogCol * ambientBrightness * daylight;
			Vector3 radiosity = (fogTransmission * radiosityBrightness + radiosityOffset) *
			                    renderer.GetSunlight();
			sunlightScale.SetValue(sunScale.x, sunScale.y, sunScale.z);
			ambientScale.SetValue(ambient.x, ambient.y, ambient.z);
			radiosityScale.SetValue(radiosity.x, radiosity.y, radiosity.z);

			fogDistance.SetValue(renderer.GetFogDistance());

			lensColorTexture.SetValue(0);
			lensDepthTexture.SetValue(1);
			lensShadowMapTexture.SetValue(2);
			ditherTexture.SetValue(3);
			ambientShadowTexture.SetValue(4);
			radiosityTexture.SetValue(5);
			noiseTexture.SetValue(6);

			std::uint32_t frame = renderer.GetFrameNumber() % 4;
			ditherOffset.SetValue((float)(frame & 1) * 0.5F, (float)((frame >> 1) & 1) * 0.5F);

			// composite to the final image
			GLColorBuffer output = input.GetManager()->CreateBufferHandle();

			dev.Enable(IGLDevice::Blend, false);
			qr.SetCoordAttributeIndex(lensPosition());

			dev.ActiveTexture(0);
			dev.BindTexture(IGLDevice::Texture2D, input.GetTexture());

			dev.ActiveTexture(1);
			dev.BindTexture(IGLDevice::Texture2D, input.GetManager()->GetDepthTexture());

			dev.ActiveTexture(2);
			dev.BindTexture(IGLDevice::Texture2D, renderer.GetMapShadowRenderer()->GetTexture());

			dev.ActiveTexture(3);
			ditherPattern->Bind(IGLDevice::Texture2D);
			dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMagFilter, IGLDevice::Nearest);
			dev.TexParamater(IGLDevice::Texture2D, IGLDevice::TextureMinFilter, IGLDevice::Nearest);

			dev.ActiveTexture(4);
			dev.BindTexture(IGLDevice::Texture3D, ambientShadowRenderer->GetTexture());
			dev.ActiveTexture(5);
			dev.BindTexture(IGLDevice::Texture3D, radiosityRenderer->GetTextureFlat());
			dev.ActiveTexture(6);
			dev.BindTexture(IGLDevice::Texture2D, noiseTex);

			dev.BindFramebuffer(IGLDevice::Framebuffer, output.GetFramebuffer());
			dev.Viewport(0, 0, output.GetWidth(), output.GetHeight());
			qr.Draw();
			dev.ActiveTexture(0);
			dev.BindTexture(IGLDevice::Texture2D, 0);

			return output;
		}
	} // namespace draw
} // namespace spades