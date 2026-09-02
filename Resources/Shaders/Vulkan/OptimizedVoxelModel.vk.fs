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

// Inputs from vertex shader
layout(location = 0) in vec4 textureCoord;
layout(location = 1) in vec3 fogDensity;
layout(location = 2) in float flatShading;

// Uniform buffers
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

layout(binding = 1) uniform ModelUniforms {
	vec3 fogColor;
	float padding1;
	vec3 customColor;
	float modelOpacity;
} model;

// Textures
layout(binding = 2) uniform sampler2D ambientOcclusionTexture;
layout(binding = 3) uniform sampler2D modelTexture;

// Output
layout(location = 0) out vec4 fragColor;

// Shadow/lighting functions (simplified for now)
vec3 EvaluateSunLight() {
	// TODO: Implement proper shadow evaluation
	return vec3(0.6);
}

vec3 EvaluateAmbientLight(float detailAmbientOcclusion) {
	// NOTE: This standalone .vk.fs shader is a leftover scaffold and is
	// not used by the live voxel-model pipeline (VulkanOptimizedVoxelModel
	// uses BasicModelVertexColor.{vert,frag} where directional radiosity
	// from VulkanRadiosityRenderer is sampled — see BasicModelVertexColor.frag).
	// Keep the cheap fallback here so the file still compiles.
	return vec3(0.4) * detailAmbientOcclusion;
}

void main() {
	vec4 texData = texture(modelTexture, textureCoord.xy);

	// Emissive material flag is encoded in AOID
	bool isEmissive = (texData.w == 1.0);

	// model color
	fragColor = vec4(texData.xyz, 1.0);
	if (dot(fragColor.xyz, vec3(1.0)) < 0.0001)
		fragColor.xyz = model.customColor;

	// linearize
	fragColor.xyz *= fragColor.xyz;

	// ambient occlusion
	float aoID = texData.w * (255.0 / 256.0);

	float aoY = aoID * 16.0;
	float aoX = fract(aoY);
	aoY = floor(aoY) / 16.0;

	vec2 ambientOcclusionCoord = vec2(aoX, aoY);
	ambientOcclusionCoord += fract(textureCoord.zw) * (15.0 / 256.0);
	ambientOcclusionCoord += 0.5 / 256.0;

	float ao = texture(ambientOcclusionTexture, ambientOcclusionCoord).x;
	vec3 diffuseShading = EvaluateAmbientLight(ao);
	diffuseShading += vec3(flatShading) * EvaluateSunLight();

	// apply diffuse shading
	if (!isEmissive)
		fragColor.xyz *= diffuseShading;

	// apply fog fading
	fragColor.xyz = mix(fragColor.xyz, model.fogColor, fogDensity);
	// Write linear; the swapchain blit (UNORM->SRGB) encodes for display.

	// Only valid in the ghost pass - Blending is disabled for most models
	fragColor.w = model.modelOpacity;
}
