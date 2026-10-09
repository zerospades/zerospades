/*
 Copyright (c) 2013 Fran6nd

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#version 450
#extension GL_GOOGLE_include_directive : require

// A soft sprite: an instance per sprite, its quad made here from the vertex index
// and brought to the front of the volume it stands for, which the fragment shader
// fades where the scene's depth cuts into.

#include "SpriteView.glsl"

layout(location = 0) in vec4 centerRadiusAttribute;
layout(location = 1) in vec4 colorAttribute;
layout(location = 2) in float angleAttribute;

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 texCoord;
layout(location = 2) out vec4 fogDensity;
layout(location = 3) out vec4 depthRange;

void main() {
	vec3 center = centerRadiusAttribute.xyz;
	float radius = centerRadiusAttribute.w;
	vec2 corner = SpriteCorner(gl_VertexIndex);
	vec3 pos = SpriteCornerPosition(center, radius, angleAttribute, corner);

	vec3 eye = spriteView.eye.xyz;
	vec3 front = spriteView.front.xyz;

	// Move sprite to the front of the volume
	float centerDepth = dot(center - eye, front);
	depthRange.xy = vec2(centerDepth) + vec2(-1.0, 1.0) * radius;

	// Clip the volume by the near clip plane
	float frontDepth = depthRange.x;
	frontDepth = max(frontDepth, 0.3);
	frontDepth = min(frontDepth, depthRange.y);
	depthRange.w = frontDepth;

	// Along the lines of sight, so the sprite still covers the same pixels: the
	// quad faces the camera, so all of it is at `centerDepth`, and scaling it about
	// the eye brings all of it to `frontDepth`. A centre at or behind the eye has
	// no such projection to keep.
	if (centerDepth > 0.001)
		pos = eye + (pos - eye) * (frontDepth / centerDepth);
	else
		pos += front * (frontDepth - centerDepth);

	gl_Position = spriteView.projectionView * vec4(pos, 1.0);

	color = colorAttribute;

	// Sprite texture coord
	texCoord.xy = corner * 0.5 + 0.5;

	// Depth texture coord (screen space).
	// The Y-flip viewport maps NDC_y → framebuffer_y inversely, so UV_y = 0.5 - NDC_y * 0.5.
	vec2 ndc = gl_Position.xy / gl_Position.w;
	texCoord.zw = vec2(0.5 + ndc.x * 0.5, 0.5 - ndc.y * 0.5);

	fogDensity = vec4(SpriteFogDensity(pos));

	// Precompute depth range values for fragment shader
	float nearFar = spriteView.nearFar.x * spriteView.nearFar.y;
	depthRange.z = 1.0 / (depthRange.y - depthRange.w);
	depthRange.y = depthRange.x;
	depthRange.x *= -depthRange.z;

	depthRange.y /= nearFar;
	depthRange.z *= nearFar;
}
