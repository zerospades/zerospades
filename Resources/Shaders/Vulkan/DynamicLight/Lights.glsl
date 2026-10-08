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

// The light the frame's dynamic lights bring to a point of a lit surface, for the
// fragment shaders of the scene's passes. A point reads only the lights of the
// cluster of the view it lies in; one outside the view, as the water's mirror sees
// it, reads them all.

// The descriptor set every lit pipeline binds the lights to, after the map's
// textures (0) and the model shadows (1)
#define DYNAMIC_LIGHT_SET 2
#define DYNAMIC_LIGHT_CLUSTER_ACCESS readonly
#include "Table.glsl"

// The images of the frame's spotlights: `VulkanDynamicLightClusters::MaxImages`.
// Unused ones hold a white image.
#define DYNAMIC_LIGHT_IMAGES 4
layout(set = DYNAMIC_LIGHT_SET, binding = 3) uniform sampler2D
  dynamicLightImages[DYNAMIC_LIGHT_IMAGES];

/**
 * Image `image` at `coord`, of the most detailed level: it is read where only some
 * fragments of a quad got that far, which leaves no derivatives to pick one with.
 * Each image is named by a constant index, as sampler arrays need without the
 * device features for anything else.
 */
vec3 DynamicLightImage(int image, vec2 coord) {
	switch (image) {
		case 0: return textureLod(dynamicLightImages[0], coord, 0.0).xyz;
		case 1: return textureLod(dynamicLightImages[1], coord, 0.0).xyz;
		case 2: return textureLod(dynamicLightImages[2], coord, 0.0).xyz;
		case 3: return textureLod(dynamicLightImages[3], coord, 0.0).xyz;
		default: return vec3(1.0);
	}
}

/**
 * The light that light `i` brings to `position`, on a surface facing `normal`,
 * shaped by its cone, its image and its reach, but not by how the surface faces
 * it; `direction` is set to the unit vector towards the light.
 */
vec3 DynamicLightIncidence(uint i, vec3 position, vec3 normal, out vec3 direction) {
	direction = vec3(0.0, 0.0, 1.0);

	vec4 originReach = dynamicLights[i].originReach;
	vec4 kind = dynamicLights[i].kind;

	vec3 lightPosition = originReach.xyz;
	if (kind.x == DYNAMIC_LIGHT_LINEAR) {
		// The nearest point of the segment stands for all of it.
		vec4 linear = dynamicLights[i].linearDirectionLength;
		float along = dot(position - lightPosition, linear.xyz);
		lightPosition += linear.xyz * clamp(along, 0.0, linear.w);
	}

	vec3 toLight = lightPosition - position;
	float distance = length(toLight);
	if (distance >= originReach.w)
		return vec3(0.0);

	direction = toLight / max(distance, 1.0e-6);

	// A surface facing away gets nothing.
	if (dot(direction, normal) <= 0.0)
		return vec3(0.0);

	vec3 image = vec3(1.0);
	float coneFalloff = 1.0;
	if (kind.x == DYNAMIC_LIGHT_SPOT) {
		vec3 coord = (dynamicLights[i].spotMatrix * vec4(position, 1.0)).xyw;

		// Nothing behind the light
		if (coord.z <= 0.0)
			return vec3(0.0);

		// A soft edge around the cone: the image spans [0, 1] with the beam's axis
		// at its centre, so this is 1 at the image's edge.
		vec2 onImage = coord.xy / coord.z;
		float coneDistance = length(onImage - 0.5) * 2.0;
		coneFalloff = 1.0 - smoothstep(0.8, DYNAMIC_LIGHT_SPOT_FADE_END, coneDistance);
		if (coneFalloff <= 0.0)
			return vec3(0.0);

		image = DynamicLightImage(int(kind.y), onImage);
	}

	vec4 colorReachInversed = dynamicLights[i].colorReachInversed;
	float reachLeft = max(1.0 - distance * colorReachInversed.w, 0.0);
	float attenuation = reachLeft * reachLeft;

	return colorReachInversed.xyz * (attenuation * coneFalloff) * image;
}

vec3 EvaluateDynamicLight(uint i, vec3 position, vec3 normal) {
	vec3 direction;
	vec3 incidence = DynamicLightIncidence(i, position, normal, direction);
	return incidence * max(dot(direction, normal), 0.0);
}

/** The light every dynamic light of the frame casts on `position`, facing `normal`
 * (unit length), unshadowed. */
vec3 EvaluateDynamicLights(vec3 position, vec3 normal) {
	uvec4 counts = dynamicLightFrame.counts;
	if (counts.w == 0u)
		return vec3(0.0);

	// Where the point is in the view, as the clusters were laid out
	vec3 relative = position - dynamicLightFrame.eyeNear.xyz;
	float depth = dot(relative, dynamicLightFrame.forward.xyz);
	vec2 onView = vec2(dot(relative, dynamicLightFrame.right.xyz) * dynamicLightFrame.right.w,
	                   dot(relative, dynamicLightFrame.up.xyz) * dynamicLightFrame.up.w) /
	              max(depth, 1.0e-6);

	vec3 light = vec3(0.0);
	if (depth >= dynamicLightFrame.eyeNear.w && all(lessThanEqual(abs(onView), vec2(1.0)))) {
		uvec2 tile = min(uvec2((onView * 0.5 + 0.5) * vec2(counts.xy)), counts.xy - 1u);
		float slice = floor(log(depth / dynamicLightFrame.eyeNear.w) * dynamicLightFrame.forward.w);
		uint layer = uint(clamp(slice, 0.0, float(counts.z - 1u)));
		uint base = ((layer * counts.y + tile.y) * counts.x + tile.x) * DYNAMIC_LIGHT_MASK_WORDS;

		for (uint word = 0u; word < DYNAMIC_LIGHT_MASK_WORDS; word++) {
			uint mask = dynamicLightClusterMasks[base + word];
			while (mask != 0u) {
				uint bit = uint(findLSB(mask));
				mask &= mask - 1u;
				light += EvaluateDynamicLight(word * 32u + bit, position, normal);
			}
		}
	} else {
		for (uint i = 0u; i < counts.w; i++)
			light += EvaluateDynamicLight(i, position, normal);
	}
	return light;
}
