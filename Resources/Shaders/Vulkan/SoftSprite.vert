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

layout(push_constant) uniform PushConstants {
	mat4 projectionViewMatrix;
	vec3 rightVector;
	float _pad1;
	vec3 upVector;
	float _pad2;
	vec3 frontVector;
	float _pad3;
	vec3 viewOriginVector;
	float _pad4;
	vec3 fogColor;
	float fogDistance;
	vec2 zNearFar;
} pc;

layout(location = 0) in vec3 positionAttribute;
layout(location = 1) in float radiusAttribute;
layout(location = 2) in vec3 spritePosAttribute;
layout(location = 3) in vec4 colorAttribute;

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 texCoord;
layout(location = 2) out vec4 fogDensity;
layout(location = 3) out vec4 depthRange;

void main() {
	vec3 center = positionAttribute;
	vec3 pos = center;
	float radius = radiusAttribute;

	vec3 right = pc.rightVector * radius;
	vec3 up = pc.upVector * radius;

	float angle = spritePosAttribute.z;
	float c = cos(angle), s = sin(angle);
	vec2 sprP;
	sprP.x = dot(spritePosAttribute.xy, vec2(c, -s));
	sprP.y = dot(spritePosAttribute.xy, vec2(s, c));
	sprP *= radius;
	pos += right * sprP.x;
	pos += up * sprP.y;

	// Move sprite to the front of the volume
	float centerDepth = dot(center - pc.viewOriginVector, pc.frontVector);
	depthRange.xy = vec2(centerDepth) + vec2(-1.0, 1.0) * radius;

	// Clip the volume by the near clip plane
	float frontDepth = depthRange.x;
	frontDepth = max(frontDepth, 0.3);
	frontDepth = min(frontDepth, depthRange.y);
	depthRange.w = frontDepth;

	pos += pc.frontVector * (frontDepth - centerDepth);

	gl_Position = pc.projectionViewMatrix * vec4(pos, 1.0);

	color = colorAttribute;

	// Sprite texture coord
	texCoord.xy = spritePosAttribute.xy * 0.5 + 0.5;

	// Depth texture coord (screen space).
	// The Y-flip viewport maps NDC_y → framebuffer_y inversely, so UV_y = 0.5 - NDC_y * 0.5.
	vec2 ndc = gl_Position.xy / gl_Position.w;
	texCoord.zw = vec2(0.5 + ndc.x * 0.5, 0.5 - ndc.y * 0.5);

	// Fog
	vec2 horzRelativePos = pos.xy - pc.viewOriginVector.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	float density = clamp(horzDistance / (pc.fogDistance * pc.fogDistance), 0.0, 1.0);
	fogDensity = vec4(density);

	// Precompute depth range values for fragment shader
	depthRange.z = 1.0 / (depthRange.y - depthRange.w);
	depthRange.y = depthRange.x;
	depthRange.x *= -depthRange.z;

	depthRange.y /= (pc.zNearFar.x * pc.zNearFar.y);
	depthRange.z *= (pc.zNearFar.x * pc.zNearFar.y);
}
