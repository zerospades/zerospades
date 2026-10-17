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

#pragma once

#include "GLDynamicLightShader.h"

namespace spades {
	namespace draw {
		class GLProgram;
		class GLRenderer;

		/**
		 * Sets up a sprite program for `SpriteLight.glsl`: the light that reaches the
		 * sprites that aren't shaded as volumes (`r_softParticles` 0 and 1), the
		 * daylight and the dynamic lights scattered through them.
		 *
		 * A sprite draw takes many sprites, so it takes the frame's dynamic lights
		 * nearest the eye, up to `GLDynamicLightShader::MaxLightsPerDraw`.
		 */
		class GLSpriteLight {
			GLDynamicLightShader lightShader;

		public:
			/** The first texture stage the dynamic lights take; the sprite programs
			 * use the ones before it. */
			static constexpr int TextureStage = 2;

			/** Sets `program`, in use, up for this frame's light, and leaves texture
			 * stage 0 active. */
			void SetUp(GLRenderer&, GLProgram* program);
		};
	} // namespace draw
} // namespace spades
