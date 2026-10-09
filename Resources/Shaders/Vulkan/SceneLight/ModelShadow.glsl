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

// The shadows the scene's models cast in the sun, as `VulkanShadowMapRenderer`
// binds its cascades (set 1 of the lit pipelines).

#ifndef SCENE_LIGHT_MODEL_SHADOW_GLSL
#define SCENE_LIGHT_MODEL_SHADOW_GLSL

layout(set = 1, binding = 0) uniform ShadowSampling {
	mat4 cascadeMatrix[3];
	int enabled;
} shadowSampling;
layout(set = 1, binding = 1) uniform sampler2D modelShadowMap0;
layout(set = 1, binding = 2) uniform sampler2D modelShadowMap1;
layout(set = 1, binding = 3) uniform sampler2D modelShadowMap2;

// Sample one cascade with a 2x2 filtered depth compare, the manual equivalent
// of the sampler2DShadow GL uses (Shadow/Model.fs). Filtering the comparison
// results rather than taking one hard compare keeps the term continuous, so it
// cannot flip wholesale between frames.
float SampleModelCascade(sampler2D tex, vec3 c) {
	vec2 uv = c.xy * 0.5 + 0.5; // clip [-1,1] -> texcoord [0,1]
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || c.z < 0.0 || c.z > 1.0)
		return 1.0; // outside this cascade: treat as lit, as GL's clamped map does

	vec2 texSize = vec2(textureSize(tex, 0));
	vec2 texel = 1.0 / texSize;
	vec2 coord = uv * texSize - 0.5;
	vec2 frac = fract(coord);
	vec2 base = (floor(coord) + 0.5) * texel;

	// Local Z 0 = sun side; an occluder with smaller stored depth is nearer the
	// sun, so this fragment is shadowed. Bias avoids self-shadow acne.
	float ref = c.z - 0.0015;
	float s00 = step(ref, texture(tex, base).r);
	float s10 = step(ref, texture(tex, base + vec2(texel.x, 0.0)).r);
	float s01 = step(ref, texture(tex, base + vec2(0.0, texel.y)).r);
	float s11 = step(ref, texture(tex, base + texel).r);

	return mix(mix(s00, s10, frac.x), mix(s01, s11, frac.x), frac.y);
}

// Dynamic (player/grenade) shadow term, combined multiplicatively with the
// map shadow. `coord0` to `coord2` are the point's coordinates in each cascade
// and `viewDepth` its distance along the camera's axis.
//
// The cascade is picked by camera-axis depth against the same split distances
// the cascade boxes were fitted to, exactly as GL does. The previous approach
// -- try cascade 0, fall through to 1 then 2 when the coordinate lands outside
// the box -- decoupled selection from the fit: the boxes are refitted from the
// camera frustum every frame, so a fragment near a boundary was reassigned to a
// different cascade from one frame to the next and the shadow term flipped with
// it. That is worst when the view axis lines up with the sun, which is when the
// boxes are most elongated and their boundaries sweep fastest.
float ModelShadowVisibility(vec3 coord0, vec3 coord1, vec3 coord2, float viewDepth) {
	if (shadowSampling.enabled == 0)
		return 1.0;
	if (viewDepth < 12.0)
		return SampleModelCascade(modelShadowMap0, coord0);
	else if (viewDepth < 40.0)
		return SampleModelCascade(modelShadowMap1, coord1);
	else
		return SampleModelCascade(modelShadowMap2, coord2);
}

#endif
