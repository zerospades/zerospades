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
	vec3 modelOrigin;
	float fogDistance;
	vec3 viewOrigin;
	float lightRadius;
	vec3 fogColor;
	float lightRadiusInversed;
	vec3 lightOrigin;
	float lightType; // 0=point, 1=linear, 2=spotlight
	vec3 lightColor;
	float lightLinearLength;
	vec3 lightLinearDirection;
	float _pad;
	mat4 lightSpotMatrix;
} pc;

layout(location = 0) in uvec3 positionAttribute;
layout(location = 1) in uvec2 aoCoordAttribute;
layout(location = 2) in uvec3 colorAttribute;
layout(location = 3) in ivec3 normalAttribute;

layout(location = 0) out vec4 color;
layout(location = 1) out vec3 lightPos;
layout(location = 2) out vec3 lightNormal;
layout(location = 3) out vec3 lightTexCoord;
layout(location = 4) out vec3 fogDensity;

void main() {
	vec3 position = vec3(positionAttribute);
	vec4 worldPos = vec4(position + pc.modelOrigin, 1.0);
	gl_Position = pc.projectionViewMatrix * worldPos;

	// Vertex color
	vec3 vertexColor = vec3(colorAttribute) / 255.0;
	vertexColor *= vertexColor; // linearize
	color = vec4(vertexColor, 1.0);

	// Normal
	vec3 normal = normalize(vec3(normalAttribute));
	lightNormal = normal;

	// Light position computation
	vec3 lightPosition = pc.lightOrigin;
	if (pc.lightType == 1.0) {
		// Linear light: closest point on line segment
		float d = dot(worldPos.xyz - pc.lightOrigin, pc.lightLinearDirection);
		d = clamp(d, 0.0, pc.lightLinearLength);
		lightPosition += pc.lightLinearDirection * d;
	}
	lightPos = lightPosition - worldPos.xyz;

	// Spotlight projection coordinates
	lightTexCoord = (pc.lightSpotMatrix * worldPos).xyw;

	// Fog density
	vec2 horzRelativePos = worldPos.xy - pc.viewOrigin.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	fogDensity = vec3(min(horzDistance / (pc.fogDistance * pc.fogDistance), 1.0));
}
