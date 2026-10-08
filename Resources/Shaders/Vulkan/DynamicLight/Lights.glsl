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

// `VulkanMapOccupancy`: each block's clearance, 0 for a solid block; every block
// within `c - 1` of one with a clearance `c` is clear.
layout(set = DYNAMIC_LIGHT_SET, binding = 4) uniform usampler3D dynamicLightMapOccupancy;

// The most steps a walk takes, each to the next block or out of a clear cube: more
// than a light's reach needs. A walk that has not got through by then stops where it
// got to, so that it errs on the side of darkness rather than lighting what a wall
// may hide.
#define DYNAMIC_LIGHT_MAP_MAX_STEPS 256

/**
 * How far along the segment from `from` to `to` it enters the first solid block,
 * from 0 at `from` to 1 at `to`, or 2 if it enters none. The block `from` lies in
 * is not looked at, and neither is the one `to` lies in. The walk crosses open air
 * a clear cube at a time.
 */
float DynamicLightMapWalk(vec3 from, vec3 to) {
	ivec3 mapSize = textureSize(dynamicLightMapOccupancy, 0);
	vec3 delta = to - from;

	vec3 cell = floor(from);
	vec3 target = floor(to);
	vec3 stepDirection = sign(delta);
	vec3 moving = abs(stepDirection);

	// The walk's progress, from 0 at `from` to 1 at `to`: `tMax` where it crosses
	// the next boundary on each axis, `tDelta` between two of them. An axis it
	// doesn't move along is never crossed.
	vec3 tDelta = 1.0 / max(abs(delta), vec3(1.0e-6));
	vec3 tMax = (stepDirection * (cell - from) + max(stepDirection, vec3(0.0))) * tDelta;
	tMax = mix(vec3(2.0), tMax, moving);

	// Where the walk entered `cell`. The first block is the one it starts in, which
	// is not looked at.
	float tEnter = 0.0;
	bool advance = true;

	for (int i = 0; i < DYNAMIC_LIGHT_MAP_MAX_STEPS; i++) {
		if (advance) {
			if (tMax.x < tMax.y && tMax.x < tMax.z) {
				tEnter = tMax.x;
				cell.x += stepDirection.x;
				tMax.x += tDelta.x;
			} else if (tMax.y < tMax.z) {
				tEnter = tMax.y;
				cell.y += stepDirection.y;
				tMax.y += tDelta.y;
			} else {
				tEnter = tMax.z;
				cell.z += stepDirection.z;
				tMax.z += tDelta.z;
			}
			if (tEnter > 1.0)
				return 2.0;
		}
		advance = true;

		if (all(equal(cell, target)))
			return 2.0;

		// Above the map is open sky, and below it solid ground, as `GameMap` has it.
		if (cell.z < 0.0)
			continue;
		if (cell.z >= float(mapSize.z))
			return tEnter;

		// The map wraps around horizontally.
		ivec3 texel = ivec3(cell);
		texel.xy = ((texel.xy % mapSize.xy) + mapSize.xy) % mapSize.xy;
		float clearance = float(texelFetch(dynamicLightMapOccupancy, texel, 0).r);
		if (clearance < 0.5)
			return tEnter;
		if (clearance < 1.5)
			continue;

		// Every block of the cube within `clearance - 1` of this one is clear: leave
		// it in one go, through the face the walk reaches first.
		vec3 low = cell - (clearance - 1.0);
		vec3 high = cell + clearance;
		vec3 exitFace = mix(low, high, max(stepDirection, vec3(0.0)));
		vec3 tExits = mix(vec3(2.0), abs(exitFace - from) * tDelta, moving);
		float tExit = min(tExits.x, min(tExits.y, tExits.z));

		// The segment ends inside the cube, so nothing is in the way.
		if (tExit >= 1.0)
			return 2.0;

		// Into the block past that face, which is looked at next.
		vec3 exits = step(tExits, vec3(tExit)) * moving;
		vec3 inside = clamp(floor(from + delta * tExit), low, high - 1.0);
		cell = mix(inside, exitFace + min(stepDirection, vec3(0.0)), exits);
		tMax = (stepDirection * (cell - from) + max(stepDirection, vec3(0.0))) * tDelta;
		tMax = mix(vec3(2.0), tMax, moving);
		tEnter = tExit;
		advance = false;
	}
	return tEnter;
}

/**
 * 1 if `lightPosition` is seen from `position`, on a surface facing `normal`, and 0
 * if the map is in the way. The first-person view's models are drawn in front of
 * the world wherever they really are, even half inside a wall, so for them it is
 * seen from the eye instead. The walk starts in the block in front of the surface,
 * so the block the surface belongs to doesn't hide its own light.
 */
float DynamicLightMapVisibility(vec3 position, vec3 normal, vec3 lightPosition) {
	if (dynamicLightFrame.mapOcclusion < 0.5)
		return 1.0;
	vec3 from = gl_FragCoord.z < dynamicLightFrame.firstPersonDepthEnd
	              ? dynamicLightFrame.eyeNear.xyz
	              : position + normal * 0.01;
	return DynamicLightMapWalk(from, lightPosition) > 1.0 ? 1.0 : 0.0;
}

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
 * shaped by its cone, its image and its reach, and hidden by the map, but not
 * by how the surface faces it; `direction` is set to the unit vector towards the light.
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

	// Last, as it is the dearest part
	float visibility = DynamicLightMapVisibility(position, normal, lightPosition);

	return colorReachInversed.xyz * (attenuation * coneFalloff * visibility) * image;
}

vec3 EvaluateDynamicLight(uint i, vec3 position, vec3 normal) {
	vec3 direction;
	vec3 incidence = DynamicLightIncidence(i, position, normal, direction);
	return incidence * max(dot(direction, normal), 0.0);
}

/** The light every dynamic light of the frame casts on `position`, facing `normal`
 * (unit length), where the map doesn't hide it. */
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
