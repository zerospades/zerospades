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

#ifndef SCENE_LIGHT_TABLE_GLSL
#define SCENE_LIGHT_TABLE_GLSL

// The frame's dynamic lights and the clusters of the view they are binned into,
// as `VulkanSceneLights` writes them. The includer defines
// `SCENE_LIGHT_SET`, the descriptor set they are bound to, and
// `DYNAMIC_LIGHT_CLUSTER_ACCESS`, how it uses the cluster masks.

// The most lights a frame takes: `VulkanSceneLights::MaxLights`.
#define DYNAMIC_LIGHT_MAX 256
// The 32-bit words of a cluster's mask, a bit for each light
#define DYNAMIC_LIGHT_MASK_WORDS (DYNAMIC_LIGHT_MAX / 32)

// How far past the edge of its image a spotlight still lights, as a fraction of
// the image's half width: `VulkanDynamicLight::SpotFadeEnd`.
#define DYNAMIC_LIGHT_SPOT_FADE_END 1.1

// `VulkanSceneLights`' occlusion maps: tiles of
// `DYNAMIC_LIGHT_OCCLUSION_TILE` texels each, `DYNAMIC_LIGHT_OCCLUSION_TILES_PER_ROW`
// to a row of the atlas, each texel holding how far from its light the ray through
// it gets before it enters a solid block.
#define DYNAMIC_LIGHT_OCCLUSION_TILE 128
#define DYNAMIC_LIGHT_OCCLUSION_TILES_PER_ROW 8
#define DYNAMIC_LIGHT_OCCLUSION_MAX_TILES 64

#define DYNAMIC_LIGHT_POINT 0.0
#define DYNAMIC_LIGHT_LINEAR 1.0
#define DYNAMIC_LIGHT_SPOT 2.0

struct DynamicLight {
	// xyz: origin, w: reach
	vec4 originReach;
	// xyz: colour, w: 1 / reach
	vec4 colorReachInversed;
	// Projects a world-space point onto a spotlight's image, [0, 1] across it after
	// the divide
	mat4 spotMatrix;
	// xyz: a linear light's direction, w: its length
	vec4 linearDirectionLength;
	// x: `DYNAMIC_LIGHT_POINT`, `_LINEAR` or `_SPOT`; y: its image in
	// `dynamicLightImages`, or -1 for none; z: its occlusion map tile, or -1 for
	// none
	vec4 kind;
	// The sphere holding everything it lights, in the view's axes: x right, y up,
	// z ahead of the eye
	vec4 viewSphere;
	// A spotlight's occlusion map is traced along these unit axes, its beam's
	// (`forward`) and its image's: xyz. `right.w` is how much wider a texel of the
	// tile gets per block away from the light.
	vec4 traceRight;
	vec4 traceUp;
	vec4 traceForward;
};

layout(set = SCENE_LIGHT_SET, binding = 0, std140) uniform DynamicLightFrame {
	// xyz: the eye, w: where the first slice of clusters starts ahead of it
	vec4 eyeNear;
	// xyz: the view's right axis, w: 1 / the tangent of half its width
	vec4 right;
	// xyz: the view's up axis, w: 1 / the tangent of half its height
	vec4 up;
	// xyz: the view's forward axis, w: slices per unit of the depth's logarithm
	vec4 forward;
	// The tangents of half the view's width and height
	vec2 tangents;
	// 1 when the map hides the lights, 0 without a map to walk
	float mapOcclusion;
	// The depth the first-person view's models are drawn in front of, nearer than
	// anything of the world
	float firstPersonDepthEnd;
	// xyz: the clusters across, up and deep; w: the lights
	uvec4 counts;
	// The light each occlusion map tile is traced for, four to an element
	uvec4 occlusionTileLights[DYNAMIC_LIGHT_OCCLUSION_MAX_TILES / 4];
} dynamicLightFrame;

layout(set = SCENE_LIGHT_SET, binding = 1, std430) readonly buffer DynamicLightList {
	DynamicLight dynamicLights[];
};

// `DYNAMIC_LIGHT_MASK_WORDS` per cluster, across, then up, then deep
layout(set = SCENE_LIGHT_SET, binding = 2, std430) DYNAMIC_LIGHT_CLUSTER_ACCESS buffer
  DynamicLightClusterMasks {
	uint dynamicLightClusterMasks[];
};

/** How far ahead of the eye slice `slice` of the clusters starts. */
float DynamicLightSliceStart(float slice) {
	return dynamicLightFrame.eyeNear.w * exp(slice / dynamicLightFrame.forward.w);
}

#endif
