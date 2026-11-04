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

#include "GLSpriteLight.h"
#include "GLProgram.h"
#include "GLProgramUniform.h"
#include "GLRenderer.h"
#include "IGLDevice.h"
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		void GLSpriteLight::SetUp(GLRenderer& renderer, GLProgram* program) {
			SPADES_MARK_FUNCTION();

			static GLProgramUniform daylight("daylight");
			daylight(program);
			daylight.SetValue(renderer.GetDaylight());

			// A sprite is lit where it is, not from the eye.
			static GLProgramUniform occludedFromEye("dynamicLightOccludedFromEye");
			occludedFromEye(program);
			occludedFromEye.SetValue(0);

			lightShader.SetUpSingleDraw(&renderer, program, renderer.GetDynamicLights(),
			                            TextureStage, renderer.GetSceneDef().viewOrigin,
			                            [](const GLDynamicLight&) { return true; });

			renderer.GetGLDevice().ActiveTexture(0);
		}
	} // namespace draw
} // namespace spades
