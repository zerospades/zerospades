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

// Ghost (semi-transparent) variant of BasicModelVertexColorPhys.frag.
// Identical physical lighting, but outputs alpha = 0.5 for SRC_ALPHA blending.
//
// Ghosts intentionally skip directional radiosity — they render as a
// translucent silhouette (placement preview, etc.), where the cheaper
// hemisphere fallback is good enough and avoids extra texture bandwidth.
// The VS outputs `radiosityTextureCoord` / `normalVarying` at locations
// 10-11; we leave them unmatched here, which Vulkan permits.

layout(set = 0, binding = 0) uniform sampler2D mapShadowTexture;
layout(set = 0, binding = 1) uniform sampler3D ambientShadowTexture;

layout(push_constant) uniform PushConstants {
	mat4 projectionViewMatrix;
	mat4 modelMatrix;
	vec3 modelOrigin;
	float fogDensity;
	vec3 customColor;
	float _pad;
	vec3 fogColor;
	float mirrorClipZ; // water-plane Z in the reflection pass (else +inf)
	vec3 sunDirection;
	float _pad3;
	mat4 viewMatrix;
	vec3 viewOrigin;
} pushConstants;

layout(location = 0) in vec4 color;
layout(location = 1) in vec3 ambientLight;     // hemisphere ambient fallback
layout(location = 2) in vec3 customColor;
layout(location = 3) in vec3 shadowCoord;
layout(location = 4) in vec3 fogDensity;
layout(location = 5) in vec3 inFogColor;
layout(location = 6) in vec3 viewSpaceCoord;
layout(location = 7) in vec3 viewSpaceNormal;
layout(location = 8) in vec3 reflectionDir;
layout(location = 9) in vec3 aoCoord;          // 3D coords into AO texture

layout(location = 0) out vec4 fragColor;

float OrenNayar(float sigma, float dotLight, float dotEye) {
	float sigma2 = sigma * sigma;
	float A = 1.0 - 0.5 * sigma2 / (sigma2 + 0.33);
	float B = 0.45 * sigma2 / (sigma2 + 0.09);
	float scale = 1.0 / A;
	float scaledB = B * scale;

	vec2 dotLightEye = clamp(vec2(dotLight, dotEye), 0.0, 1.0);
	vec2 sinLightEye = sqrt(1.0 - dotLightEye * dotLightEye);
	float alphaSin = max(sinLightEye.x, sinLightEye.y);
	float betaCos = max(dotLightEye.x, dotLightEye.y);
	float betaCos2 = betaCos * betaCos;
	float betaTan = 1.0 / sqrt(betaCos2 / max(1.0 - betaCos2, 0.001));

	vec4 vecs = vec4(dotLightEye, sinLightEye);
	float diffCos = dot(vecs.xz, vecs.yw);

	return dotLight * (1.0 + scaledB * diffCos * alphaSin * betaTan);
}

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
	float fresnel = 0.03 + 0.1 * fresnel2 * fresnel2;

	float a = m * 0.7978, ia = 1.0 - a;
	float visibility = (dotNL * ia + a) * (dotNV * ia + a);
	visibility = 0.25 / visibility;

	return distribution * fresnel * visibility;
}

void main() {
	float shadowVal = texture(mapShadowTexture, shadowCoord.xy).w;
	float shadow = (shadowVal < shadowCoord.z - 0.0001) ? 0.0 : 1.0;

	vec3 vertexColor = color.xyz;

	if (dot(vertexColor, vec3(1.0)) < 0.0001) {
		vertexColor = customColor;
	}

	vertexColor *= vertexColor;

	// Per-block ambient occlusion (matching GL MapRadiosity.fs).
	vec2 ambTexVal = texture(ambientShadowTexture, aoCoord).xy;
	float aoFactor = max(ambTexVal.x / max(ambTexVal.y, 0.25), 0.0);

	fragColor = vec4(vertexColor, pushConstants._pad);
	vec3 diffuseShading = ambientLight * aoFactor;
	float shadowing = shadow * 0.6;

	vec3 eyeVec = -normalize(viewSpaceCoord);

	// Sun direction from the renderer (single source of truth, GetSunDirection)
	vec3 sunDir = normalize(pushConstants.sunDirection);
	vec3 viewSpaceLight = normalize((pushConstants.viewMatrix * vec4(sunDir, 0.0)).xyz);

	float dotNL = max(color.w, 0.001);
	float dotNV = max(dot(viewSpaceNormal, eyeVec), 0.001);

	float fresnel2 = 1.0 - dotNV;
	float fresnel = 0.03 + 0.1 * fresnel2 * fresnel2;

	vec3 reflectWS = normalize(reflectionDir);
	float reflHemisphere = 1.0 - reflectWS.z * 0.2;
	vec3 specularShading = mix(inFogColor, vec3(1.0), 0.5) * 0.5 * reflHemisphere;

	if (shadowing > 0.0 && dotNL > 0.0) {
		float sunDiffuseShading = OrenNayar(0.8, dotNL, dotNV);
		diffuseShading += sunDiffuseShading * shadowing;

		float sunSpecularShading = CookTorrance(eyeVec, viewSpaceLight, viewSpaceNormal);
		fragColor.xyz += sunSpecularShading * shadowing;
	}

	fragColor.xyz = mix(diffuseShading * fragColor.xyz, specularShading, fresnel);

	fragColor.xyz = mix(fragColor.xyz, inFogColor, fogDensity);
	fragColor.xyz = max(fragColor.xyz, 0.0);
	// Write linear; the swapchain blit (UNORM->SRGB) encodes for display.
}
