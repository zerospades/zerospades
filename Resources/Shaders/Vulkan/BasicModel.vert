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
} pushConstants;

layout(location = 0) in uvec3 positionAttribute;
layout(location = 1) in uvec2 textureCoordAttribute;
layout(location = 2) in ivec3 normalAttribute;

layout(location = 0) out vec4 color;
layout(location = 1) out vec2 texCoord;
layout(location = 2) out vec3 normal;

void main() {
	// Convert uint8 position to float
	vec3 position = vec3(positionAttribute);
	vec4 worldPos = vec4(position + pushConstants.modelOrigin, 1.0);
	gl_Position = pushConstants.projectionViewMatrix * worldPos;

	// Convert int8 normal to float and normalize
	vec3 normalFloat = normalize(vec3(normalAttribute));

	// Simple lighting from above
	vec3 sunDir = normalize(vec3(-1.0, -1.0, -1.0));
	float lighting = max(dot(normalFloat, sunDir), 0.2);

	color = vec4(lighting, lighting, lighting, 1.0);
	texCoord = vec2(textureCoordAttribute) / 256.0;  // Basic texture coordinate scaling
	normal = normalFloat;
}
