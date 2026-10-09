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
#define SCENE_LIGHT_SET 2
#define DYNAMIC_LIGHT_CLUSTER_ACCESS readonly
#include "Table.glsl"

// The images of the frame's spotlights: `VulkanSceneLights::MaxImages`.
// Unused ones hold a white image.
#define DYNAMIC_LIGHT_IMAGES 4
layout(set = SCENE_LIGHT_SET, binding = 3) uniform sampler2D
  dynamicLightImages[DYNAMIC_LIGHT_IMAGES];

#include "MapWalk.glsl"

// The spotlights' occlusion maps, traced this frame by `SceneLight/OcclusionMap.comp`
layout(set = SCENE_LIGHT_SET, binding = 5, r32f) uniform readonly image2D
  dynamicLightOcclusionMaps;

// The widest a texel may be at a point for its tile to prove the point lit, in
// blocks: below `1 / sqrt(2)`, no block can stand in the way of the point without
// crossing one of the four rays around it.
#define DYNAMIC_LIGHT_OCCLUSION_MAX_TEXEL_WIDTH 0.7
// How far short of where it meets the point's surface a ray may stop and still
// count as reaching it, in blocks
#define DYNAMIC_LIGHT_OCCLUSION_TOLERANCE 0.1

/**
 * Whether this fragment belongs to the first-person view's models, which are drawn
 * in front of the world wherever they really are, even half inside a wall: the map
 * hides the lights from them as it does from the eye.
 */
bool DynamicLightIsFirstPerson() {
	return gl_FragCoord.z < dynamicLightFrame.firstPersonDepthEnd;
}

/**
 * Whether spotlight `i`, at `lightPosition`, certainly reaches `position`, on a
 * surface facing `normal`: the rays of the four texels around it in its occlusion
 * map `tile` all get to that surface. Each ray is held to where it meets the plane
 * of the surface, which a ray grazing a floor meets well before or after the point.
 * Where the texels are too wide to be sure, it isn't certain.
 *
 * Rays that all stop short don't prove the point dark: it may be seen through a gap
 * that, taken at a slant, is narrower than the rays are apart.
 */
bool DynamicLightOcclusionMapReaches(uint i, int tile, vec3 position, vec3 normal,
                                     vec3 lightPosition) {
	vec4 right = dynamicLights[i].traceRight;
	vec3 up = dynamicLights[i].traceUp.xyz;
	vec3 forward = dynamicLights[i].traceForward.xyz;

	vec3 toPoint = position - lightPosition;
	float distance = length(toPoint);
	float ahead = dot(toPoint, forward);
	if (ahead <= 1.0e-6 || distance * right.w > DYNAMIC_LIGHT_OCCLUSION_MAX_TEXEL_WIDTH)
		return false;

	// Where the point is on the tile, as it was traced
	float tileSize = float(DYNAMIC_LIGHT_OCCLUSION_TILE);
	float spread = right.w * tileSize;
	vec2 onTile = vec2(dot(toPoint, right.xyz), dot(toPoint, up)) / (ahead * spread) + 0.5;
	vec2 base = floor(onTile * tileSize - 0.5);

	ivec2 tileOrigin = ivec2(tile % DYNAMIC_LIGHT_OCCLUSION_TILES_PER_ROW,
	                         tile / DYNAMIC_LIGHT_OCCLUSION_TILES_PER_ROW) *
	                   DYNAMIC_LIGHT_OCCLUSION_TILE;
	float surfaceOffset = dot(normal, toPoint);

	for (int k = 0; k < 4; k++) {
		vec2 texel = clamp(base + vec2(float(k & 1), float(k >> 1)), vec2(0.0),
		                   vec2(tileSize - 1.0));

		// The ray through the texel's centre, as the tile was traced
		vec2 slope = ((texel + 0.5) / tileSize - 0.5) * spread;
		vec3 direction = normalize(forward + right.xyz * slope.x + up * slope.y);

		// Where it meets the surface's plane, but no farther than the point: past
		// it, the ray has passed whatever stands before the point.
		float facing = dot(normal, direction);
		float target = facing < -1.0e-4 ? min(surfaceOffset / facing, distance) : distance;

		float reach = imageLoad(dynamicLightOcclusionMaps, tileOrigin + ivec2(texel)).x;
		if (reach < target - DYNAMIC_LIGHT_OCCLUSION_TOLERANCE)
			return false;
	}
	return true;
}

/**
 * 1 if light `i`, at `lightPosition`, is seen from `position`, on a surface facing
 * `normal`, and 0 if the map is in the way. A spotlight's occlusion map proves most
 * points lit at the cost of four reads; the rest, and the first-person view's
 * models, which are seen from the eye, walk the map. The walk starts in the block
 * in front of the surface, so the block the surface belongs to doesn't hide its
 * own light.
 */
float DynamicLightMapVisibility(uint i, vec3 position, vec3 normal, vec3 lightPosition) {
	if (dynamicLightFrame.mapOcclusion < 0.5)
		return 1.0;

	bool firstPerson = DynamicLightIsFirstPerson();
	int tile = int(dynamicLights[i].kind.z);
	if (tile >= 0 && !firstPerson &&
	    DynamicLightOcclusionMapReaches(i, tile, position, normal, lightPosition))
		return 1.0;

	vec3 from = firstPerson ? dynamicLightFrame.eyeNear.xyz : position + normal * 0.01;
	return DynamicLightMapWalk(from, lightPosition, true) > 1.0 ? 1.0 : 0.0;
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
	float visibility = DynamicLightMapVisibility(i, position, normal, lightPosition);

	return colorReachInversed.xyz * (attenuation * coneFalloff * visibility) * image;
}

/**
 * The light light `i` brings to `position`, on a surface facing `normal`: diffuse,
 * with `shininess` 0, or else the Phong highlight of exponent `shininess` it makes
 * along `reflected`, the eye's ray mirrored by the surface, normalized for it, so a
 * tighter highlight is a brighter one.
 */
vec3 EvaluateDynamicLight(uint i, vec3 position, vec3 normal, vec3 reflected, float shininess) {
	vec3 direction;
	vec3 incidence = DynamicLightIncidence(i, position, normal, direction);
	float cosIncidence = max(dot(direction, normal), 0.0);
	if (shininess <= 0.0)
		return incidence * cosIncidence;

	float normalization = (shininess + 2.0) * (1.0 / (2.0 * 3.14159265));
	float lobe = pow(max(dot(reflected, direction), 0.0), shininess);
	return incidence * (lobe * normalization * cosIncidence);
}

/**
 * `EvaluateDynamicLight` summed over the lights that reach `position`: those its
 * cluster lists, or every light of the frame where it lies outside the view the
 * clusters cut up.
 */
vec3 EvaluateDynamicLightsAt(vec3 position, vec3 normal, vec3 reflected, float shininess) {
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
				light += EvaluateDynamicLight(word * 32u + bit, position, normal, reflected,
				                              shininess);
			}
		}
	} else {
		for (uint i = 0u; i < counts.w; i++)
			light += EvaluateDynamicLight(i, position, normal, reflected, shininess);
	}
	return light;
}

/** The light every dynamic light of the frame casts on `position`, facing `normal`
 * (unit length), where the map doesn't hide it. */
vec3 EvaluateDynamicLights(vec3 position, vec3 normal) {
	return EvaluateDynamicLightsAt(position, normal, vec3(0.0), 0.0);
}

/**
 * The light every dynamic light of the frame reflects off a glossy surface at
 * `position`, facing `normal`, along `reflected`: the eye's ray mirrored by the
 * surface. Both are unit length; `shininess` is the highlight's Phong exponent, and
 * the result is normalized for it. The caller weighs it by the surface's Fresnel
 * term.
 */
vec3 EvaluateDynamicLightsSpecular(vec3 position, vec3 normal, vec3 reflected, float shininess) {
	return EvaluateDynamicLightsAt(position, normal, reflected, shininess);
}
