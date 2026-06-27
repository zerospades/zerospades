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
	mat4 viewMatrix;
	vec3 rightVector;
	vec3 upVector;
	vec3 viewOriginVector;
	vec3 fogColor;
	float fogDistance;
} pc;

layout(location = 0) in vec3 positionAttribute;
layout(location = 1) in float radiusAttribute;
layout(location = 2) in vec3 spritePosAttribute;
layout(location = 3) in vec4 colorAttribute;

layout(location = 0) out vec4 color;
layout(location = 1) out vec2 texCoord;
layout(location = 2) out vec4 fogDensity;

void main() {
	vec3 pos = positionAttribute;
	float radius = radiusAttribute;

	vec3 right = pc.rightVector * radius;
	vec3 up = pc.upVector * radius;

	float angle = spritePosAttribute.z;
	float c = cos(angle);
	float s = sin(angle);
	vec2 sprP;
	sprP.x = dot(spritePosAttribute.xy, vec2(c, -s));
	sprP.y = dot(spritePosAttribute.xy, vec2(s, c));
	sprP *= radius;
	pos += right * sprP.x;
	pos += up * sprP.y;

	gl_Position = pc.projectionViewMatrix * vec4(pos, 1.0);

	color = colorAttribute;

	// Sprite texture coord
	texCoord = spritePosAttribute.xy * 0.5 + 0.5;

	// Fog calculation
	vec2 horzRelativePos = pos.xy - pc.viewOriginVector.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	float density = clamp(horzDistance / (pc.fogDistance * pc.fogDistance), 0.0, 1.0);
	fogDensity = vec4(density);
}
