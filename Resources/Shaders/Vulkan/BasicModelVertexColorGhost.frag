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

// Ghost (semi-transparent) variant of BasicModelVertexColor.frag.
// Identical lighting, but outputs alpha = pushConstants._pad (param.opacity).
//
// Ghosts intentionally skip directional radiosity — they render as a
// translucent silhouette (placement preview, etc.), where the cheaper
// hemisphere fallback is good enough and avoids extra texture bandwidth.
// The VS outputs `radiosityTextureCoord` / `normalVarying` at locations
// 7-8; we leave them unmatched here, which Vulkan permits.

layout(push_constant) uniform PushConstants {
	mat4 projectionViewMatrix;
	mat4 modelMatrix;
	vec3 modelOrigin;
	float fogDensity;
	vec3 customColor;
	float _pad; // opacity
	vec3 fogColor;
} pushConstants;

layout(set = 0, binding = 0) uniform sampler2D mapShadowTexture;
layout(set = 0, binding = 1) uniform sampler3D ambientShadowTexture;

layout(location = 0) in vec4 color;           // xyz = vertexColor, w = sun lambert
layout(location = 1) in vec3 ambientLight;     // hemisphere ambient fallback
layout(location = 2) in vec3 customColor;
layout(location = 3) in vec3 shadowCoord;
layout(location = 4) in vec3 fogDensity;
layout(location = 5) in vec3 inFogColor;
layout(location = 6) in vec3 aoCoord;          // 3D coords into AO texture

layout(location = 0) out vec4 fragColor;

void main() {
	float shadowVal = texture(mapShadowTexture, shadowCoord.xy).w;
	float shadow = (shadowVal < shadowCoord.z - 0.0001) ? 0.0 : 1.0;

	vec3 vertexColor = color.xyz;

	if (dot(vertexColor, vec3(1.0)) < 0.0001) {
		vertexColor = customColor;
	}

	vertexColor *= vertexColor;

	vec2 ambTexVal = texture(ambientShadowTexture, aoCoord).xy;
	float aoFactor = max(ambTexVal.x / max(ambTexVal.y, 0.25), 0.0);

	float sunLambert = color.w;
	vec3 sun = vec3(0.6) * sunLambert * shadow;
	fragColor = vec4(vertexColor * (ambientLight * aoFactor + sun), pushConstants._pad);

	fragColor.xyz = mix(fragColor.xyz, inFogColor, fogDensity);
	fragColor.xyz = max(fragColor.xyz, 0.0);
	// Write linear; the swapchain blit (UNORM->SRGB) encodes for display.
}
