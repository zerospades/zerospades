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
// 0 and 1), at a corner of its quad: the daylight, and the dynamic lights scattered
// through the volume the sprite stands for, so that smoke and debris show in a
// lamp's beam or a blast's flash at night rather than staying black. For the
// sprites' vertex shaders.

#include "DynamicLight/Lights.glsl"

// How much of the day's light there is, from 0 at night to 1 in full daylight
uniform float daylight;

/**
 * The light on the corner at `corner` of a sprite centred on `center`, seen along
 * `front`: the normal of the volume's bulge there leans towards the eye.
 */
vec3 SpriteLight(vec3 center, vec3 corner, vec3 front) {
	vec3 normal = normalize((corner - center) - front);
	return vec3(daylight) + EvaluateDynamicLightsScattered(corner, normal);
}

/** `color`, of a sprite that scatters light, lit by `light`: in linear light, hence
 * the root for a gamma-encoded colour. */
vec4 LightSprite(vec4 color, vec3 light) {
#if LINEAR_FRAMEBUFFER
	color.xyz *= light;
#else
	color.xyz *= sqrt(light);
#endif
	return color;
}
