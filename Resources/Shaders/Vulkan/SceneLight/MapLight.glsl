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

// The map's own light: its shadow from the sun, the sky's light it lets reach a
// point, and the sunlight it bounces, as `VulkanMapRenderer` binds them (set 0 of
// the lit pipelines). Needs `SunSky.glsl`.

#ifndef SCENE_LIGHT_MAP_LIGHT_GLSL
#define SCENE_LIGHT_MAP_LIGHT_GLSL

layout(set = 0, binding = 0) uniform sampler2D mapShadowTexture;
layout(set = 0, binding = 1) uniform sampler3D ambientShadowTexture;
layout(set = 0, binding = 2) uniform sampler3D radiosityTextureFlat;
layout(set = 0, binding = 3) uniform sampler3D radiosityTextureX;
layout(set = 0, binding = 4) uniform sampler3D radiosityTextureY;
layout(set = 0, binding = 5) uniform sampler3D radiosityTextureZ;
layout(set = 0, binding = 6) uniform sampler2D ambientOcclusionAtlas; // Gfx/AmbientOcclusion.png

/** 1 where the sun reaches over the map, 0 where the map shadows it, at
 * `shadowCoord`: the map shadow's coordinates of the point (GL Map.fs). */
float MapShadowVisibility(vec3 shadowCoord) {
	float shadowVal = texture(mapShadowTexture, shadowCoord.xy).w;
	return (shadowVal < shadowCoord.z - 0.0001) ? 0.0 : 1.0;
}

/** How much of the sky the map lets reach `aoCoord`, from its per-block ambient
 * occlusion. */
float MapAmbientShadow(vec3 aoCoord) {
	// .x = AO accumulation, .y = sample weight (1 in air, 0 in solids)
	vec2 ambTexVal = texture(ambientShadowTexture, aoCoord).xy;
	return max(ambTexVal.x / max(ambTexVal.y, 0.25), 0.0);
}

// Linear (RGB10A2) decode of radiosity values. Mirrors GL MapRadiosity.fs
// DecodeRadiosityValue, but only the high-precision (linear) branch — the
// Vulkan port stores radiosity in A2R10G10B10_UNORM_PACK32 always.
vec3 DecodeRadiosityValue(vec3 val) {
	val *= 1023.0 / 1022.0;
	val = (val * 2.0) - 1.0;
	return val;
}

/** The sunlight the map bounces onto a surface facing `normal` at
 * `radiosityCoord` (GL MapRadiosity.fs EvaluateRadiosity), at the frame's
 * sunlight. */
vec3 MapRadiosity(vec3 radiosityCoord, vec3 normal) {
	vec3 radiosity = DecodeRadiosityValue(texture(radiosityTextureFlat, radiosityCoord).xyz);
	radiosity += normal.x * DecodeRadiosityValue(texture(radiosityTextureX, radiosityCoord).xyz);
	radiosity += normal.y * DecodeRadiosityValue(texture(radiosityTextureY, radiosityCoord).xyz);
	radiosity += normal.z * DecodeRadiosityValue(texture(radiosityTextureZ, radiosityCoord).xyz);
	return max(radiosity, 0.0) * (1.5 * sceneSunSky.sunlight);
}

#endif
