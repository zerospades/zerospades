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

// `GLMapOccupancy`'s texture: 1 where a block is solid. Unfiltered and without
// mipmaps, so it can be sampled inside the walk's non-uniform control flow.
uniform sampler3D dynamicLightMapOccupancy;
// 1 / the map's size in blocks
uniform vec3 dynamicLightMapSizeInversed;
// 0 without a map to walk, where nothing is occluded
uniform float dynamicLightMapOcclusion;

// The most block boundaries a walk crosses: more than a light's reach across a
// block diagonal needs. A light farther than that along the walk is taken as seen.
#define DYNAMIC_LIGHT_MAP_MAX_STEPS 256

/**
 * 1 if `lightPosition` is seen from `position`, on a surface facing `normal`, and 0
 * if a solid block is in the way. The walk starts in the block in front of the
 * surface, so the block the surface belongs to doesn't hide its own light, and
 * stops at the light's block.
 */
float DynamicLightMapVisibility(vec3 position, vec3 normal, vec3 lightPosition) {
	if (dynamicLightMapOcclusion < 0.5)
		return 1.0;

	vec3 origin = position + normal * 0.01;
	vec3 delta = lightPosition - origin;

	vec3 cell = floor(origin);
	vec3 target = floor(lightPosition);
	vec3 stepDirection = sign(delta);

	// The walk's progress, from 0 at `origin` to 1 at the light: `tMax` where it
	// crosses the next boundary on each axis, `tDelta` between two of them. An
	// axis it doesn't move along is never crossed.
	vec3 tDelta = 1.0 / max(abs(delta), vec3(1.0e-6));
	vec3 tMax = (stepDirection * (cell - origin) + max(stepDirection, vec3(0.0))) * tDelta;
	tMax = mix(vec3(2.0), tMax, abs(stepDirection));

	for (int i = 0; i < DYNAMIC_LIGHT_MAP_MAX_STEPS; i++) {
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

		if (all(equal(cell, target)))
			return 1.0;

		// Above the map is open sky; the texture would repeat its top layer there.
		if (cell.z >= 0.0 &&
		    texture3D(dynamicLightMapOccupancy, (cell + 0.5) * dynamicLightMapSizeInversed).x > 0.5)
			return 0.0;
	}
	return 1.0;
}
