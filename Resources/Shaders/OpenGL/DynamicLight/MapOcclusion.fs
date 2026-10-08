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

// Whether the map stands between a lit point and a dynamic light, walked block by
// block through `GLMapOccupancy`.

// `GLMapOccupancy`'s texture: each block's clearance out of 255, `0` for a solid
// block; every block within `c - 1` of one with a clearance `c` is clear.
// Unfiltered and without mipmaps, so it can be sampled inside the walk's
// non-uniform control flow.
uniform sampler3D dynamicLightMapOccupancy;
// 1 / the map's size in blocks
uniform vec3 dynamicLightMapSizeInversed;
// 0 without a map to walk, where nothing is occluded
uniform float dynamicLightMapOcclusion;

// The most steps a walk takes, each to the next block or out of a clear cube: more
// than a light's reach needs. A light farther than that along the walk is taken as
// seen.
#define DYNAMIC_LIGHT_MAP_MAX_STEPS 256

/**
 * 1 if `lightPosition` is seen from `position`, on a surface facing `normal`, and 0
 * if a solid block is in the way. The walk starts in the block in front of the
 * surface, so the block the surface belongs to doesn't hide its own light, and
 * stops at the light's block. It crosses open air a clear cube at a time.
 */
float DynamicLightMapVisibility(vec3 position, vec3 normal, vec3 lightPosition) {
	if (dynamicLightMapOcclusion < 0.5)
		return 1.0;

	vec3 origin = position + normal * 0.01;
	vec3 delta = lightPosition - origin;

	vec3 cell = floor(origin);
	vec3 target = floor(lightPosition);
	vec3 stepDirection = sign(delta);
	vec3 moving = abs(stepDirection);

	// The walk's progress, from 0 at `origin` to 1 at the light: `tMax` where it
	// crosses the next boundary on each axis, `tDelta` between two of them. An
	// axis it doesn't move along is never crossed.
	vec3 tDelta = 1.0 / max(abs(delta), vec3(1.0e-6));
	vec3 tMax = (stepDirection * (cell - origin) + max(stepDirection, vec3(0.0))) * tDelta;
	tMax = mix(vec3(2.0), tMax, moving);

	// The first block is the one the walk starts in, which is not looked at.
	bool advance = true;

	for (int i = 0; i < DYNAMIC_LIGHT_MAP_MAX_STEPS; i++) {
		if (advance) {
			if (tMax.x < tMax.y && tMax.x < tMax.z) {
				if (tMax.x > 1.0)
					return 1.0;
				cell.x += stepDirection.x;
				tMax.x += tDelta.x;
			} else if (tMax.y < tMax.z) {
				if (tMax.y > 1.0)
					return 1.0;
				cell.y += stepDirection.y;
				tMax.y += tDelta.y;
			} else {
				if (tMax.z > 1.0)
					return 1.0;
				cell.z += stepDirection.z;
				tMax.z += tDelta.z;
			}
		}
		advance = true;

		if (all(equal(cell, target)))
			return 1.0;

		// Above the map is open sky; the texture would repeat its top layer there.
		if (cell.z < 0.0)
			continue;

		float clearance = floor(
		  texture3D(dynamicLightMapOccupancy, (cell + 0.5) * dynamicLightMapSizeInversed).x *
		    255.0 +
		  0.5);
		if (clearance < 0.5)
			return 0.0;
		if (clearance < 1.5)
			continue;

		// Every block of the cube within `clearance - 1` of this one is clear: leave
		// it in one go, through the face the walk reaches first.
		vec3 low = cell - (clearance - 1.0);
		vec3 high = cell + clearance;
		vec3 exitFace = mix(low, high, max(stepDirection, vec3(0.0)));
		vec3 tExits = mix(vec3(2.0), abs(exitFace - origin) * tDelta, moving);
		float tExit = min(tExits.x, min(tExits.y, tExits.z));

		// The light is inside the cube, so nothing is in the way.
		if (tExit >= 1.0)
			return 1.0;

		// Into the block past that face, which is looked at next.
		vec3 exits = step(tExits, vec3(tExit)) * moving;
		vec3 inside = clamp(floor(origin + delta * tExit), low, high - 1.0);
		cell = mix(inside, exitFace + min(stepDirection, vec3(0.0)), exits);
		tMax = (stepDirection * (cell - origin) + max(stepDirection, vec3(0.0))) * tDelta;
		tMax = mix(vec3(2.0), tMax, moving);
		advance = false;
	}
	return 1.0;
}
