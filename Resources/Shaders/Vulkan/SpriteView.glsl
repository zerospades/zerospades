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

// The sprites' own descriptor set (`VulkanSpriteRenderer::SpriteSet`): the view
// they are drawn in, and their image. The lit pipelines' sets come before it.

#define SPRITE_SET 3

layout(set = SPRITE_SET, binding = 0, std140) uniform SpriteView {
	mat4 projectionView;
	// The view's axes and the eye, xyz
	vec4 right;
	vec4 up;
	vec4 front;
	vec4 eye;
	// xyz: the fog colour, linear; w: the fog distance
	vec4 fogColorDistance;
	// x: the near plane's distance, y: the far plane's
	vec4 nearFar;
	// xyz: towards the sun
	vec4 sunDirection;
	// x: the area over which a soft sprite starts going to the quarter-resolution
	// layer, y: 1 / the area over which it gets there (GL's soft sprites)
	vec4 lowResolution;
} spriteView;

layout(set = SPRITE_SET, binding = 1) uniform sampler2D mainTexture;

/** The corner of the sprite's quad vertex `index` of its triangle strip is, from
 * (-1, -1) to (1, 1). */
vec2 SpriteCorner(int index) {
	return vec2(float(index & 1), float(index >> 1)) * 2.0 - 1.0;
}

/** Where `corner` of a sprite centred on `center`, `radius` across and turned by
 * `angle`, lies before it is brought to the front of its volume. */
vec3 SpriteCornerPosition(vec3 center, float radius, float angle, vec2 corner) {
	float c = cos(angle), s = sin(angle);
	vec2 turned = vec2(dot(corner, vec2(c, -s)), dot(corner, vec2(s, c))) * radius;
	return center + spriteView.right.xyz * turned.x + spriteView.up.xyz * turned.y;
}

// Which of a soft sprite's layers a pipeline draws: `VulkanSpriteRenderer::Layer`.
#define SPRITE_LAYER_EVERY 0
#define SPRITE_LAYER_FULL_RESOLUTION 1
#define SPRITE_LAYER_LOW_RESOLUTION 2

/**
 * How much of a sprite centred on `center`, `radius` across, `layer` draws: as GL
 * has it, a soft sprite that covers much of the screen goes to the quarter-
 * resolution layer, blurred before it is laid over the scene, and one that covers
 * little stays at full resolution, the two crossfading in between.
 */
float SpriteLayerFade(vec3 center, float radius, int layer) {
	if (layer == SPRITE_LAYER_EVERY)
		return 1.0;
	float depth = max(dot(center - spriteView.eye.xyz, spriteView.front.xyz), 0.01);
	float area = radius * radius * 4.0 / depth;
	float lowResolution =
	  clamp((area - spriteView.lowResolution.x) * spriteView.lowResolution.y, 0.0, 1.0);
	return layer == SPRITE_LAYER_LOW_RESOLUTION ? lowResolution : 1.0 - lowResolution;
}

/** How far into the fog a point at `position` is, horizontally, from 0 to 1. */
float SpriteFogDensity(vec3 position) {
	vec2 horizontal = position.xy - spriteView.eye.xy;
	float distance = spriteView.fogColorDistance.w;
	return clamp(dot(horizontal, horizontal) / (distance * distance), 0.0, 1.0);
}
