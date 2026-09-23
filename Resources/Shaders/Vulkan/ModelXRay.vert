/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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

// Vulkan port of OptimizedVoxelModelXRay.vs. The GL model samples a texture
// atlas where this one carries per-vertex colours, so the albedo arrives as a
// vertex attribute instead of a texture coordinate; everything else matches.
//
// The GL shader builds the fresnel term in world space from viewOriginVector.
// The view matrix is rigid, so the same angle falls out of view space, and
// pushing viewModelMatrix alone covers the fresnel, the eye vector and the
// specular normal -- one matrix instead of three.

layout(push_constant) uniform PushConstants {
	mat4 projectionViewModelMatrix;
	mat4 viewModelMatrix;
	vec3 modelOrigin;
	float _pad0;
	vec3 xrayColor;
	float _pad1;
	vec3 customColor;
	float _pad2;
	vec3 viewSpaceLight; // sun direction in view space, normalized
	float _pad3;
} pushConstants;

layout(location = 0) in uvec3 positionAttribute;
layout(location = 1) in uvec3 colorAttribute;
layout(location = 2) in ivec3 normalAttribute;

layout(location = 0) out vec3 vertexColor;
layout(location = 1) out float fresnel;
layout(location = 2) out vec3 viewSpaceCoord;
layout(location = 3) out vec3 viewSpaceNormal;

void main() {
	vec4 localPos = vec4(vec3(positionAttribute) + pushConstants.modelOrigin, 1.0);
	gl_Position = pushConstants.projectionViewModelMatrix * localPos;

	viewSpaceCoord = (pushConstants.viewModelMatrix * localPos).xyz;
	viewSpaceNormal =
		normalize((pushConstants.viewModelMatrix * vec4(normalize(vec3(normalAttribute)), 0.0)).xyz);

	vec3 viewDir = normalize(-viewSpaceCoord);
	float dotNV = max(dot(viewSpaceNormal, viewDir), 0.0);
	float f0 = 0.04;
	fresnel = f0 + (1.0 - f0) * pow(1.0 - dotNV, 4.0);

	vertexColor = vec3(colorAttribute) / 255.0;
}
