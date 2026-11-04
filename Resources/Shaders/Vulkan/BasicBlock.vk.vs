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
	mat4 projectionViewMatrix;
	mat4 viewMatrix;
	vec3 chunkPosition;
	float padding1;
	vec3 sunLightDirection;
	float padding2;
	vec3 viewOriginVector;
	float fogDistance;
} ubo;

// Vertex attributes
layout(location = 0) in vec3 positionAttribute;
layout(location = 1) in vec2 ambientOcclusionCoordAttribute;
layout(location = 2) in vec4 colorAttribute;
layout(location = 3) in vec3 normalAttribute;
layout(location = 4) in vec3 fixedPositionAttribute;

// Outputs to fragment shader
layout(location = 0) out vec2 ambientOcclusionCoord;
layout(location = 1) out vec4 color;
layout(location = 2) out vec3 fogDensity;
layout(location = 3) out vec3 normalVarying;

// Shadow/fog functions (simplified for now)
vec4 ComputeFogDensity(float poweredLength) {
	return vec4(min(poweredLength * (1.0 / ubo.fogDistance / ubo.fogDistance), 1.0));
}

void PrepareShadowForMap(vec3 vertexCoord, vec3 fixedVertexCoord, vec3 normal) {
	// TODO: Implement shadow coordinate calculation
	// For now, this is a stub
}

void main() {
	vec4 vertexPos = vec4(ubo.chunkPosition + positionAttribute, 1.0);

	gl_Position = ubo.projectionViewMatrix * vertexPos;

	// ambient occlusion
	ambientOcclusionCoord = (ambientOcclusionCoordAttribute + 0.5) * (1.0 / 256.0);

	color = colorAttribute;
	color.xyz *= color.xyz; // linearize

	// lambert reflection
	//vec3 normal = normalAttribute;
	//color.w = max(dot(normal, ubo.sunLightDirection), 0.0);

	vec2 horzRelativePos = vertexPos.xy - ubo.viewOriginVector.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	fogDensity = ComputeFogDensity(horzDistance).xyz;

	normalVarying = normalAttribute;

	vec3 fixedWorldPosition = ubo.chunkPosition + fixedPositionAttribute * 0.5;
	PrepareShadowForMap(vertexPos.xyz, fixedWorldPosition, normalAttribute);
}
