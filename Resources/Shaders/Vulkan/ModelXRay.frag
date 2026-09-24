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

// Vulkan port of OptimizedVoxelModelXRay.fs: the tinted, scanlined shape a
// revealed player is drawn as where the world hides them.

layout(push_constant) uniform PushConstants {
	mat4 projectionViewModelMatrix;
	mat4 viewModelMatrix;
	vec3 modelOrigin;
	float _pad0;
	vec3 xrayColor;
	float _pad1;
	vec3 customColor;
	float _pad2;
	vec3 viewSpaceLight;
	float _pad3;
} pushConstants;

layout(location = 0) in vec3 vertexColor;
layout(location = 1) in float fresnel;
layout(location = 2) in vec3 viewSpaceCoord;
layout(location = 3) in vec3 viewSpaceNormal;

layout(location = 0) out vec4 fragColor;

// GGX microfacet distribution; same constants as BasicModelVertexColorPhys.frag.
float GGXDistribution(float m, float dotHalf) {
	float m2 = m * m;
	float t = dotHalf * dotHalf * (m2 - 1.0) + 1.0;
	return m2 / (3.141592653 * t * t);
}

float CookTorrance(vec3 eyeVec, vec3 lightVec, vec3 normal) {
	vec3 halfVec = lightVec + eyeVec;
	halfVec = (dot(halfVec, halfVec) < 0.00000000001)
		? vec3(1.0, 0.0, 0.0) : normalize(halfVec);

	float dotNL = max(dot(normal, lightVec), 0.001);
	float dotNV = max(dot(normal, eyeVec), 0.001);
	float dotNH = max(dot(normal, halfVec), 0.001);
	float dotVH = max(dot(eyeVec, halfVec), 0.001);

	float m = 0.3;
	float distribution = GGXDistribution(m, dotNH);

	float fresnel2 = 1.0 - dotVH;
	float fresnelTerm = 0.03 + 0.1 * fresnel2 * fresnel2;

	float a = m * 0.7978, ia = 1.0 - a;
	float visibility = (dotNL * ia + a) * (dotNV * ia + a);
	visibility = 0.25 / visibility;

	return distribution * fresnelTerm * visibility;
}

void main() {
	// The team colour reaches a player's block-coloured parts through this
	// fallback, exactly as it does in the solid pass.
	vec3 albedo = vertexColor;
	if (dot(albedo, vec3(1.0)) < 0.0001)
		albedo = pushConstants.customColor;
	albedo *= albedo; // linearize

	vec3 eyeVec = normalize(-viewSpaceCoord);
	vec3 normal = normalize(viewSpaceNormal);

	float sunSpecularShading = CookTorrance(eyeVec, pushConstants.viewSpaceLight, normal);

	vec3 color = pushConstants.xrayColor * mix(0.5 + albedo * 0.5, vec3(1.0), fresnel);
	color += pushConstants.xrayColor * sunSpecularShading * 4.0;

	// No gamma encode here: this renderer's scene target is linear throughout
	// and the swapchain blit encodes for display, so GL's !LINEAR_FRAMEBUFFER
	// sqrt has no counterpart.

	float scanline = sin(gl_FragCoord.y * 2.0) * 0.5 + 0.5;
	color *= mix(0.6, 1.0, scanline);

	float alpha = mix(0.9, 1.0, fresnel) + clamp(sunSpecularShading * 0.8, 0.0, 1.0);
	alpha = min(alpha, 1.0);

	fragColor = vec4(color, alpha);
}
