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

// Uniform buffer for matrices and view data
layout(binding = 0) uniform UniformBufferObject {
	mat4 projectionViewModelMatrix;
	mat4 viewModelMatrix;
	mat4 modelMatrix;
	mat4 modelNormalMatrix;
	vec3 modelOrigin;
	float padding1;
	vec3 sunLightDirection;
	float padding2;
	vec3 viewOriginVector;
	float fogDistance;
	vec2 texScale;
	vec2 padding3;
} ubo;

// Vertex attributes
layout(location = 0) in vec3 positionAttribute;
layout(location = 1) in vec2 textureCoordAttribute;
layout(location = 2) in vec3 normalAttribute;

// Outputs to fragment shader
layout(location = 0) out vec4 textureCoord;
layout(location = 1) out vec3 fogDensity;
layout(location = 2) out float flatShading;

// Shadow/fog functions (simplified for now)
vec4 ComputeFogDensity(float poweredLength) {
	return vec4(min(poweredLength * (1.0 / ubo.fogDistance / ubo.fogDistance), 1.0));
}

void PrepareShadow(vec3 worldOrigin, vec3 normal) {
	// TODO: Implement shadow coordinate calculation
}

void main() {
	vec4 vertexPos = vec4(positionAttribute + ubo.modelOrigin, 1.0);

	gl_Position = ubo.projectionViewModelMatrix * vertexPos;

	textureCoord = textureCoordAttribute.xyxy * vec4(ubo.texScale, vec2(1.0));

	// direct sunlight
	vec3 normal = normalize((ubo.modelNormalMatrix * vec4(normalAttribute, 1.0)).xyz);
	flatShading = max(dot(normal, ubo.sunLightDirection), 0.0);

	vec3 worldPosition = (ubo.modelMatrix * vertexPos).xyz;
	vec2 horzRelativePos = worldPosition.xy - ubo.viewOriginVector.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	fogDensity = ComputeFogDensity(horzDistance).xyz;

	PrepareShadow(worldPosition, normal);
}
