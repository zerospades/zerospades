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

// The dynamic lights a draw takes, evaluated at a point the caller gives: this
// declares no varying, so a program can link it whatever its vertex shader passes.

// The most lights one draw takes: `GLDynamicLightShader::MaxLightsPerDraw`.
#define DYNAMIC_LIGHT_MAX 8

uniform int dynamicLightCount;
uniform vec3 dynamicLightOrigin[DYNAMIC_LIGHT_MAX];
uniform vec3 dynamicLightColor[DYNAMIC_LIGHT_MAX];
uniform float dynamicLightRadius[DYNAMIC_LIGHT_MAX];
uniform float dynamicLightRadiusInversed[DYNAMIC_LIGHT_MAX];
// Projects onto the light's image; a point or linear light's maps everything to
// its centre, so it can still be sampled
uniform mat4 dynamicLightSpotMatrix[DYNAMIC_LIGHT_MAX];
// `1` for a spotlight, whose cone and image shape the light, `0` otherwise
uniform float dynamicLightIsSpot[DYNAMIC_LIGHT_MAX];
uniform float dynamicLightIsLinear[DYNAMIC_LIGHT_MAX];
uniform vec3 dynamicLightLinearDirection[DYNAMIC_LIGHT_MAX];
uniform float dynamicLightLinearLength[DYNAMIC_LIGHT_MAX];
// Shared by every spotlight of the draw
uniform sampler2D dynamicLightProjectionTexture;

// MapOcclusion.fs
float DynamicLightMapVisibility(vec3 position, vec3 normal, vec3 lightPosition);

// Whether the map hides the lights from the eye rather than from the lit point:
// set for the first-person view's models, which are drawn in front of the world
// wherever they really are, even half inside a wall.
uniform bool dynamicLightOccludedFromEye;
uniform vec3 dynamicLightEye;

/**
 * The light that light `i` brings to `position`, on a surface facing `normal`,
 * shaped by its cone, its image and its reach, and hidden by the map, but not by
 * how the surface faces it; `direction` is set to the unit vector towards the
 * light.
 */
vec3 DynamicLightIncidence(int i, vec3 position, vec3 normal, out vec3 direction) {
	// The image is sampled before anything below can return: a texture's mipmap level
	// is undefined inside control flow that differs between fragments.
	vec3 lightTexCoord = (dynamicLightSpotMatrix[i] * vec4(position, 1.0)).xyw;
	vec3 texValue = texture2DProj(dynamicLightProjectionTexture, lightTexCoord).xyz;
	texValue = mix(vec3(1.0), texValue, dynamicLightIsSpot[i]);

	vec3 lightPosition = dynamicLightOrigin[i];
	if (dynamicLightIsLinear[i] > 0.5) {
		// Linear light approximation - choose the closest point on the light
		// geometry as the representative light source
		float d = dot(position - lightPosition, dynamicLightLinearDirection[i]);
		lightPosition += dynamicLightLinearDirection[i] * clamp(d, 0.0, dynamicLightLinearLength[i]);
	}

	vec3 lightPos = lightPosition - position;
	direction = normalize(lightPos);

	// A surface facing away gets nothing: no need to walk the map for it.
	if (dot(direction, normal) <= 0.0)
		return vec3(0.0);

	float coneFalloff = 1.0;
	if (dynamicLightIsSpot[i] > 0.5) {
		// Nothing behind the light source
		if (lightTexCoord.z <= 0.0)
			return vec3(0.0);

		// Soft cone edge instead of a hard cut. The projected coordinates span
		// [0, 1] with the cone axis at (0.5, 0.5), so measure from there and
		// rescale to 1.0 at the cone edge.
		vec2 coneCoord = lightTexCoord.xy / lightTexCoord.z - vec2(0.5);
		float coneDistance = length(coneCoord) * 2.0;
		// Smooth falloff at cone edge (0.8 to 1.1 normalized)
		coneFalloff = smoothstep(1.1, 0.8, coneDistance);
		if (coneFalloff <= 0.0)
			return vec3(0.0);
	}

	// attenuation
	float distance = length(lightPos);
	if (distance >= dynamicLightRadius[i])
		return vec3(0.0);
	distance = max(1.0 - distance * dynamicLightRadiusInversed[i], 0.0);
	float attenuation = distance * distance;

	vec3 occlusionOrigin = dynamicLightOccludedFromEye ? dynamicLightEye : position;
	return dynamicLightColor[i] * (attenuation * coneFalloff) * texValue *
	       DynamicLightMapVisibility(occlusionOrigin, normal, lightPosition);
}

vec3 EvaluateDynamicLight(int i, vec3 position, vec3 normal) {
	vec3 direction;
	vec3 incidence = DynamicLightIncidence(i, position, normal, direction);

	// diffuse lighting
	return incidence * max(dot(direction, normal), 0.0);
}

/** The light every dynamic light of the draw casts on `position`, facing `normal`
 * (unit length), unshadowed. */
vec3 EvaluateDynamicLights(vec3 position, vec3 normal) {
	vec3 light = vec3(0.0);
	for (int i = 0; i < DYNAMIC_LIGHT_MAX; i++) {
		if (i >= dynamicLightCount)
			break;
		light += EvaluateDynamicLight(i, position, normal);
	}
	return light;
}
