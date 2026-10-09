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

/** How far into the fog a point at `position` is, horizontally, from 0 to 1. */
float SpriteFogDensity(vec3 position) {
	vec2 horizontal = position.xy - spriteView.eye.xy;
	float distance = spriteView.fogColorDistance.w;
	return clamp(dot(horizontal, horizontal) / (distance * distance), 0.0, 1.0);
}
