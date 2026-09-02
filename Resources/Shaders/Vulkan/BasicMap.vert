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
	float _pad;
	vec3 fogColor;
	float _pad2;
	vec3 sunDirection; // points toward the sun (renderer GetSunDirection)
} pushConstants;

// Set 1: model-shadow cascade matrices (owned by VulkanShadowMapRenderer).
layout(set = 1, binding = 0) uniform ShadowSampling {
	mat4 cascadeMatrix[3];
	int enabled;
} shadowSampling;

layout(location = 0) in uvec3 positionAttribute;
layout(location = 1) in uvec2 aoCoordAttribute;
layout(location = 2) in uvec3 colorAttribute;  // colorRed, colorGreen, colorBlue
layout(location = 3) in ivec3 normalAttribute;
layout(location = 4) in ivec3 fixedPositionAttribute; // face-center * 2 (chunk-local)

layout(location = 0) out vec4 color;          // xyz = linearized vertex color, w = sun lambert
layout(location = 1) out vec3 ambientLight;    // ambient lighting component (hemisphere fallback)
layout(location = 2) out vec3 fogDensity;
layout(location = 3) out vec3 outFogColor;
layout(location = 4) out vec3 shadowCoord;     // shadow map coordinates
layout(location = 5) out vec3 aoCoord;         // 3D coords into ambient-occlusion texture
layout(location = 6) out vec3 radiosityTextureCoord; // 3D coords into radiosity textures
layout(location = 7) out vec3 normalVarying;   // per-face normal in world space
layout(location = 8) out vec2 ambientOcclusionCoord; // 2D coords into Gfx/AmbientOcclusion atlas
layout(location = 9) out vec3 modelShadowCoord0;     // light-clip coords per cascade
layout(location = 10) out vec3 modelShadowCoord1;
layout(location = 11) out vec3 modelShadowCoord2;
// Distance from the camera along the view axis, used to pick the cascade the
// same way GL does (Shadow/Model.vs passes it as shadowMapCoord1.w). For a
// standard perspective projection gl_Position.w is exactly that depth, so no
// view matrix is needed here.
layout(location = 12) out float shadowViewDepth;

void main() {
	// Convert uint8 position to float
	vec3 position = vec3(positionAttribute);
	vec4 worldPos = vec4(position + pushConstants.modelOrigin, 1.0);
	gl_Position = pushConstants.projectionViewMatrix * worldPos;

	// Convert int8 normal to float and normalize
	vec3 normalFloat = normalize(vec3(normalAttribute));

	// Sun direction from the renderer (single source of truth; matches the
	// shadow projection and lens flare).
	vec3 sunDir = normalize(pushConstants.sunDirection);
	float lambert = max(dot(normalFloat, sunDir), 0.0);

	// Convert color from uint8 [0,255] to float [0,1] and linearize
	vec3 vertexColor = vec3(colorAttribute) / 255.0;
	vertexColor *= vertexColor;

	// Ambient color matching GL GLShadowShader: fog * 0.5 with a minimum
	// luminance floor of 0.35 so things stay visible even when the sky is
	// near-black. (fogColor is already linearized in C++.)
	float hemisphere = 1.0 - normalFloat.z * 0.2;
	vec3 ac = pushConstants.fogColor * 0.5;
	float L = (ac.x + ac.y + ac.z) / 3.0;
	ac += ((ac + 0.003) / (L + 0.003)) * max(0.35 - L, 0.0);
	vec3 ambient = ac * hemisphere;

	// Pass vertex color (xyz) and sun lambert (w) to fragment shader
	color = vec4(vertexColor, lambert);
	ambientLight = ambient;

	// Shadow coordinate matching OpenGL Map.vs: PrepareForMapShadow
	// mapShadowCoord.y -= mapShadowCoord.z (diagonal sun projection)
	// mapShadowCoord.z /= 255.0 (normalize height)
	// mapShadowCoord.xy /= 512.0 (normalize to texture coords)
	// Use the face-center "fixed position" (matches GL fixedPositionAttribute)
	// instead of the raw vertex position. Vertices lie exactly on voxel
	// boundaries; sampling the map-shadow texture there picks the neighboring
	// column and leaks a lit sliver along face edges (e.g. top of north faces
	// seen from the south). The face center is safely inside the voxel.
	vec3 wPos = vec3(fixedPositionAttribute) * 0.5 + pushConstants.modelOrigin;
	shadowCoord = vec3(wPos.x / 512.0, (wPos.y - wPos.z) / 512.0, wPos.z / 255.0);

	// Fog density based on horizontal distance (matching SW/GL implementation)
	vec2 horzRelativePos = worldPos.xy - pushConstants.viewOrigin.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	fogDensity = vec3(min(horzDistance / (pushConstants.fogDistance * pushConstants.fogDistance), 1.0));
	outFogColor = pushConstants.fogColor;

	// AO 3D-texture coords. World position with z+1 (the 0-th slice is the
	// "below ground" guard plane), divided by texture extent. Map dimensions
	// are 512x512x64 in this game, so the texture is 512x512x65.
	aoCoord = (wPos + vec3(0.0, 0.0, 1.0)) / vec3(512.0, 512.0, 65.0);

	// 2D AO atlas coords (matches GL BasicBlock.vs). The atlas is 256x256 with
	// 16x16 precomputed AO tiles; aoCoordAttribute holds tile_x*16 + corner
	// offset on each axis (range 0..255). +0.5 centres the sample.
	ambientOcclusionCoord = (vec2(aoCoordAttribute) + 0.5) * (1.0 / 256.0);

	// Radiosity 3D-texture coords (matches GL MapRadiosity.vs).
	radiosityTextureCoord = wPos / vec3(512.0, 512.0, 64.0);
	normalVarying = normalFloat;

	// Per-cascade light-clip coords for model-shadow sampling. Ortho => w == 1,
	// so .xyz is the clip coord (xy in [-1,1], z in [0,1]).
	modelShadowCoord0 = (shadowSampling.cascadeMatrix[0] * worldPos).xyz;
	modelShadowCoord1 = (shadowSampling.cascadeMatrix[1] * worldPos).xyz;
	modelShadowCoord2 = (shadowSampling.cascadeMatrix[2] * worldPos).xyz;
	shadowViewDepth = gl_Position.w;
}
