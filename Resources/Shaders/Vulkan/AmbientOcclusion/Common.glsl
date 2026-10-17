/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

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

// What the screen-space ambient occlusion's dispatches share: the view's depth,
// read where it was written, and turned into distances along the view.

#ifndef AMBIENT_OCCLUSION_COMMON_GLSL
#define AMBIENT_OCCLUSION_COMMON_GLSL

// Depth that is the far plane or beyond: the sky, nothing to occlude
const float kBackgroundDepth = 0.999999;

/** The distance along the view of a depth buffer value `depth`, as the scene's
 * projection (`SceneDefinition::ToVulkanProjectionMatrix`) maps it to [0, 1] */
float LinearDepth(float depth, vec2 zNearFar) {
	return zNearFar.x * zNearFar.y / mix(zNearFar.y, zNearFar.x, depth);
}

/** The depth texel `texel` of `depthTexture`, held to its edges */
float DepthTexel(sampler2D depthTexture, ivec2 texel) {
	ivec2 last = textureSize(depthTexture, 0) - 1;
	return texelFetch(depthTexture, clamp(texel, ivec2(0), last), 0).x;
}

#endif
