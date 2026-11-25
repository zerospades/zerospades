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

// Whether the map's radiosity and ambient-shadow volumes light the scene
// (`r_radiosity`). Without them the sky alone lights it, as GL's
// MapRadiosityNull, and the volumes bound are placeholders never read.
layout(constant_id = 0) const int USE_RADIOSITY = 0;

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

/**
 * The diffuse light the map and the sky shed on a surface facing `normal`,
 * with `detailAO` its own (per-vertex) ambient occlusion and `ssao` the
 * screen-space one, 1 without: GL's EvaluateRadiosity, from MapRadiosity.fs or,
 * without radiosity, MapRadiosityNull.fs.
 */
vec3 MapAmbientLight(vec3 radiosityCoord, vec3 aoCoord, vec3 normal, float detailAO,
                     float ssao) {
	detailAO *= ssao;
	if (USE_RADIOSITY != 0) {
		// Mix the two ambient occlusions, the map's coarse one and the
		// surface's detail, as GL does
		float amb = MapAmbientShadow(aoCoord);
		amb = mix(sqrt(amb * detailAO), min(amb, detailAO), 0.5);
		amb *= (0.8 - normal.z * 0.2) * sceneSunSky.daylight;
		return MapRadiosity(radiosityCoord, normal) * ssao + amb * sceneSunSky.ambientLight;
	}
	float hemisphere = 1.0 - normal.z * 0.2;
	return mix(sceneSunSky.skyLight, vec3(1.0), 0.5) *
	       (0.5 * detailAO * hemisphere * sceneSunSky.daylight);
}

/**
 * The light the map and the sky reflect off a glossy surface facing `normal`
 * towards the eye, reflected along `direction`, through `detailAO` and `ssao`
 * as `MapAmbientLight`: GL's EvaluateSoftReflections, from MapRadiosity.fs or,
 * without radiosity, MapRadiosityNull.fs.
 */
vec3 MapSoftReflections(vec3 radiosityCoord, vec3 aoCoord, vec3 normal, vec3 direction,
                        float detailAO, float ssao) {
	detailAO *= ssao;
	if (USE_RADIOSITY != 0) {
		float amb = max(texture(ambientShadowTexture, aoCoord).x, 0.0);
		amb *= amb; // darken
		amb = mix(sqrt(amb * detailAO), min(amb, detailAO), 0.5);
		amb *= (0.8 - normal.z * 0.2) * sceneSunSky.daylight;
		return MapRadiosity(radiosityCoord, normal) * ssao + sceneSunSky.skyLight * amb;
	}
	return sceneSunSky.skyLight * ((direction.z * -0.5 + 0.5) * detailAO * sceneSunSky.daylight);
}

#endif
