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
// than a light's reach needs. A walk that has not got through by then stops where it
// got to, so that it errs on the side of darkness rather than lighting what a wall
// may hide.
#define DYNAMIC_LIGHT_MAP_MAX_STEPS 256

/**
 * How far along the segment from `from` to `to` it enters the first solid block,
 * from 0 at `from` to 1 at `to`, or 2 if it enters none. A walk out of steps
 * stops where it got to. The block `from` lies in is
 * not looked at; with `stopAtTarget`, neither is the one `to` lies in. The walk
 * crosses open air a clear cube at a time.
 */
float DynamicLightMapWalk(vec3 from, vec3 to, bool stopAtTarget) {
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

		if (stopAtTarget && all(equal(cell, target)))
			return 2.0;

		// Above the map is open sky, and below it solid ground, as `GameMap` has it;
		// the texture would repeat its top and bottom layers there.
		if (cell.z < 0.0)
			continue;
		if (cell.z * dynamicLightMapSizeInversed.z >= 1.0)
			return tEnter;

		float clearance = floor(
		  texture3D(dynamicLightMapOccupancy, (cell + 0.5) * dynamicLightMapSizeInversed).x *
		    255.0 +
		  0.5);
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
 * if a solid block is in the way. The walk starts in the block in front of the
 * surface, so the block the surface belongs to doesn't hide its own light, and
 * stops at the light's block.
 */
float DynamicLightMapVisibility(vec3 position, vec3 normal, vec3 lightPosition) {
	if (dynamicLightMapOcclusion < 0.5)
		return 1.0;
	return DynamicLightMapWalk(position + normal * 0.01, lightPosition, true) > 1.0 ? 1.0 : 0.0;
}
