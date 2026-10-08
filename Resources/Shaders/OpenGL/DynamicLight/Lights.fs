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

// How many lights the draw takes, and their rows in `dynamicLightTable`, four to a
// vector
uniform int dynamicLightCount;
uniform vec4 dynamicLightRows[2];

// `GLDynamicLightTable`: a row of texels per light of the frame,
//   0: origin, reach;  1: colour, 1 / reach;
//   2 to 5: the matrix projecting onto the light's image, by column (a point or
//           linear light's maps everything to its centre, so it can still be
//           sampled);
//   6: a linear light's direction and length;  7: is it a spotlight, is it linear
uniform sampler2D dynamicLightTable;
// 1 / the rows the table has
uniform float dynamicLightTableRowsInversed;

vec4 DynamicLightTexel(float row, float column) {
	return texture2D(dynamicLightTable,
	                 vec2((column + 0.5) * (1.0 / 8.0), (row + 0.5) * dynamicLightTableRowsInversed));
}

/** The row in the table of the draw's light `i`. */
float DynamicLightRow(int i) {
	vec4 rows = i < 4 ? dynamicLightRows[0] : dynamicLightRows[1];
	int lane = i < 4 ? i : i - 4;
	if (lane == 0)
		return rows.x;
	if (lane == 1)
		return rows.y;
	if (lane == 2)
		return rows.z;
	return rows.w;
}

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
 *
 * Most fragments of a draw lie outside the reach or the cone of most of its lights,
 * so the light is read from the table a part at a time, and each test leaves
 * before the next part is read. The spotlight image has no mipmaps for the same
 * reason: it is sampled where only some fragments of a quad got that far.
 */
vec3 DynamicLightIncidence(int i, vec3 position, vec3 normal, out vec3 direction) {
	direction = vec3(0.0, 0.0, 1.0);

	float row = DynamicLightRow(i);
	vec4 originReach = DynamicLightTexel(row, 0.0);
	vec4 kind = DynamicLightTexel(row, 7.0);

	vec3 lightPosition = originReach.xyz;
	if (kind.y > 0.5) {
		// Linear light approximation - choose the closest point on the light
		// geometry as the representative light source
		vec4 linear = DynamicLightTexel(row, 6.0);
		float d = dot(position - lightPosition, linear.xyz);
		lightPosition += linear.xyz * clamp(d, 0.0, linear.w);
	}

	// attenuation
	vec3 lightPos = lightPosition - position;
	float distance = length(lightPos);
	if (distance >= originReach.w)
		return vec3(0.0);

	direction = lightPos / max(distance, 1.0e-6);

	// A surface facing away gets nothing: no need to walk the map for it.
	if (dot(direction, normal) <= 0.0)
		return vec3(0.0);

	vec3 texValue = vec3(1.0);
	float coneFalloff = 1.0;
	if (kind.x > 0.5) {
		mat4 spotMatrix = mat4(DynamicLightTexel(row, 2.0), DynamicLightTexel(row, 3.0),
		                       DynamicLightTexel(row, 4.0), DynamicLightTexel(row, 5.0));
		vec3 lightTexCoord = (spotMatrix * vec4(position, 1.0)).xyw;

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

		texValue = texture2DProj(dynamicLightProjectionTexture, lightTexCoord).xyz;
	}

	vec4 colorReachInversed = DynamicLightTexel(row, 1.0);
	float reachLeft = max(1.0 - distance * colorReachInversed.w, 0.0);
	float attenuation = reachLeft * reachLeft;

	vec3 occlusionOrigin = dynamicLightOccludedFromEye ? dynamicLightEye : position;
	return colorReachInversed.xyz * (attenuation * coneFalloff) * texValue *
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
