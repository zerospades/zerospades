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

// The light that reaches a sprite that isn't shaded as a volume (`r_softParticles`
// 0 and 1), at a corner of its quad: the daylight, as GL dims such sprites by, and
// the dynamic lights scattered through the volume the sprite stands for, so that
// smoke and debris show in a lamp's beam or a blast's flash at night rather than
// staying black. For vertex shaders, which include `SpriteView.glsl` first.

#ifndef SPRITE_LIGHT_GLSL
#define SPRITE_LIGHT_GLSL

#define SCENE_LIGHT_VERTEX_STAGE
#include "SceneLight/Lights.glsl"

/**
 * The light on the corner at `corner` of a sprite centred on `center`: the
 * normal of the volume's bulge there leans towards the eye as GL's lit sprites'
 * does.
 */
vec3 SpriteLight(vec3 center, vec3 corner) {
	vec3 normal = normalize((corner - center) - spriteView.front.xyz);
	return vec3(sceneSunSky.daylight) + EvaluateDynamicLightsScattered(corner, normal);
}

#endif
