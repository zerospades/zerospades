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

// Selects the radiosity-on permutation (matches GL MapRadiosity.fs) vs the
// no-radiosity permutation (matches GL OptimizedVoxelModel.fs + MapRadiosityNull.fs).
// Driven by r_radiosity at pipeline-creation time.
layout(constant_id = 0) const int USE_RADIOSITY = 0;

layout(set = 0, binding = 0) uniform sampler2D mapShadowTexture;
layout(set = 0, binding = 1) uniform sampler3D ambientShadowTexture;
layout(set = 0, binding = 2) uniform sampler3D radiosityTextureFlat;
layout(set = 0, binding = 3) uniform sampler3D radiosityTextureX;
layout(set = 0, binding = 4) uniform sampler3D radiosityTextureY;
layout(set = 0, binding = 5) uniform sampler3D radiosityTextureZ;
layout(set = 0, binding = 6) uniform sampler2D ambientOcclusionAtlas; // Gfx/AmbientOcclusion.png

layout(set = 1, binding = 0) uniform ShadowSampling {
	mat4 cascadeMatrix[3];
	int enabled;
} shadowSampling;
layout(set = 1, binding = 1) uniform sampler2D modelShadowMap0;
layout(set = 1, binding = 2) uniform sampler2D modelShadowMap1;
layout(set = 1, binding = 3) uniform sampler2D modelShadowMap2;

layout(location = 0) in vec4 color;           // xyz = vertexColor, w = sun lambert
layout(location = 1) in vec3 ambientLight;     // hemisphere ambient fallback (kept for VS↔FS compat)
layout(location = 2) in vec3 customColor;
layout(location = 3) in vec3 shadowCoord;
layout(location = 4) in vec3 fogDensity;
layout(location = 5) in vec3 inFogColor;
layout(location = 6) in vec3 aoCoord;          // 3D coords into AO texture
layout(location = 7) in vec3 radiosityTextureCoord;
layout(location = 8) in vec3 normalVarying;
layout(location = 9) in vec2 ambientOcclusionCoord; // 2D coords into AO atlas
layout(location = 10) in float waterClip;      // <0 = below the reflection plane
layout(location = 11) in vec3 modelShadowCoord0;    // light-clip coords per cascade
layout(location = 12) in vec3 modelShadowCoord1;
layout(location = 13) in vec3 modelShadowCoord2;
layout(location = 14) in float shadowViewDepth;     // camera-axis depth, for cascade choice

layout(location = 0) out vec4 fragColor;

// Same cascade sampling as BasicMap.frag -- see the rationale there for both
// the 2x2 filtered compare and the depth-based cascade choice. Local Z 0 = sun
// side; a smaller stored depth means an occluder nearer the sun. Bias avoids
// self-shadow acne (models render into these cascades themselves).
float SampleModelCascade(sampler2D tex, vec3 c) {
	vec2 uv = c.xy * 0.5 + 0.5;
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || c.z < 0.0 || c.z > 1.0)
		return 1.0;

	vec2 texSize = vec2(textureSize(tex, 0));
	vec2 texel = 1.0 / texSize;
	vec2 coord = uv * texSize - 0.5;
	vec2 frac = fract(coord);
	vec2 base = (floor(coord) + 0.5) * texel;

	float ref = c.z - 0.0015;
	float s00 = step(ref, texture(tex, base).r);
	float s10 = step(ref, texture(tex, base + vec2(texel.x, 0.0)).r);
	float s01 = step(ref, texture(tex, base + vec2(0.0, texel.y)).r);
	float s11 = step(ref, texture(tex, base + texel).r);

	return mix(mix(s00, s10, frac.x), mix(s01, s11, frac.x), frac.y);
}

float EvaluteModelShadow() {
	if (shadowSampling.enabled == 0)
		return 1.0;
	if (shadowViewDepth < 12.0)
		return SampleModelCascade(modelShadowMap0, modelShadowCoord0);
	else if (shadowViewDepth < 40.0)
		return SampleModelCascade(modelShadowMap1, modelShadowCoord1);
	else
		return SampleModelCascade(modelShadowMap2, modelShadowCoord2);
}

vec3 DecodeRadiosityValue(vec3 val) {
	val *= 1023.0 / 1022.0;
	val = (val * 2.0) - 1.0;
	return val;
}

void main() {
	// Reflection pass: discard fragments below the water plane so underwater
	// players never enter the mirror (waterClip is +inf-based in the scene pass).
	if (waterClip < 0.0)
		discard;

	// Evaluate map shadow (matching OpenGL Map.fs: EvaluateMapShadow)
	float shadowVal = texture(mapShadowTexture, shadowCoord.xy).w;
	float shadow = (shadowVal < shadowCoord.z - 0.0001) ? 0.0 : 1.0;
	shadow *= EvaluteModelShadow(); // model-on-model cascades (set 1)

	vec3 vertexColor = color.xyz;

	// If the vertex color is very dark/black (near zero), replace with customColor
	// This allows team colors to override black voxels in player/weapon models
	if (dot(vertexColor, vec3(1.0)) < 0.0001) {
		vertexColor = customColor;
	}

	// Linearize color (after team color substitution, matching OpenGL)
	vertexColor *= vertexColor;

	vec3 nrm = normalize(normalVarying);
	float sunLambert = color.w;
	vec3 sun = vec3(0.6) * sunLambert * shadow;

	vec3 diffuse;
	if (USE_RADIOSITY != 0) {
		// MapRadiosity.fs path — 3D radiosity + 3D AO modulating sky-ambient.
		vec2 ambTexVal = texture(ambientShadowTexture, aoCoord).xy;
		float aoFactor = max(ambTexVal.x / max(ambTexVal.y, 0.25), 0.0);

		vec3 radiosity = DecodeRadiosityValue(texture(radiosityTextureFlat, radiosityTextureCoord).xyz);
		radiosity += nrm.x * DecodeRadiosityValue(texture(radiosityTextureX, radiosityTextureCoord).xyz);
		radiosity += nrm.y * DecodeRadiosityValue(texture(radiosityTextureY, radiosityTextureCoord).xyz);
		radiosity += nrm.z * DecodeRadiosityValue(texture(radiosityTextureZ, radiosityTextureCoord).xyz);
		radiosity = max(radiosity, 0.0) * 1.5;

		// Blend in the per-vertex detail AO from the 2D atlas, as GL does via
		// EvaluateAmbientLight(ao) -> EvaluateRadiosity. Without it the sqrt()
		// lift is missing and ambient-lit faces come out darker than GL.
		// See the no-radiosity branch below for the v-flip rationale.
		vec2 aoAtlasUV = vec2(ambientOcclusionCoord.x, 1.0 - ambientOcclusionCoord.y);
		float detailAO = texture(ambientOcclusionAtlas, aoAtlasUV).x;
		float amb = mix(sqrt(aoFactor * detailAO), min(aoFactor, detailAO), 0.5);

		float aoTerm = amb * (0.8 - nrm.z * 0.2);
		vec3 ambientColor = inFogColor * 0.5;
		float ambL = (ambientColor.x + ambientColor.y + ambientColor.z) / 3.0;
		ambientColor += ((ambientColor + 0.003) / (ambL + 0.003)) * max(0.35 - ambL, 0.0);

		diffuse = radiosity + aoTerm * ambientColor + sun;
	} else {
		// MapRadiosityNull.fs + GL OptimizedVoxelModel.fs path — per-face AO
		// from the 2D atlas (baked per-vertex aoID), modulating
		// mix(fog, white, 0.5) ambient.  See BasicMap.frag for the v-flip
		// rationale (VulkanImageManager y-flips images on upload).
		vec2 aoUV = vec2(ambientOcclusionCoord.x, 1.0 - ambientOcclusionCoord.y);
		float ao = texture(ambientOcclusionAtlas, aoUV).x;
		float hemisphere = 1.0 - nrm.z * 0.2;
		vec3 ambientColor = mix(inFogColor, vec3(1.0), 0.5);
		diffuse = ambientColor * (0.5 * ao * hemisphere) + sun;
	}

	fragColor = vec4(vertexColor * diffuse, 1.0);

	// Apply fog fading
	fragColor.xyz = mix(fragColor.xyz, inFogColor, fogDensity);
	fragColor.xyz = max(fragColor.xyz, 0.0);
	// Write linear; the swapchain blit (UNORM->SRGB) encodes for display.
}
