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
#extension GL_GOOGLE_include_directive : require

#include "SceneView.glsl"


layout(location = 0) in vec4 color;           // xyz = vertexColor, w = sun lambert
layout(location = 1) in vec3 ambientLight;     // hemisphere ambient fallback
layout(location = 2) in vec3 fogDensity;
layout(location = 3) in vec3 inFogColor;
layout(location = 4) in vec3 shadowCoord;
layout(location = 5) in vec3 viewSpaceCoord;
layout(location = 6) in vec3 viewSpaceNormal;
layout(location = 7) in vec3 reflectionDir;
layout(location = 8) in vec3 aoCoord;          // 3D coords into AO texture
layout(location = 9) in vec3 radiosityTextureCoord;
layout(location = 10) in vec3 normalVarying;
// World-space position, where the dynamic lights are evaluated
layout(location = 11) in vec3 worldPosition;
layout(location = 12) in vec2 ambientOcclusionCoord; // 2D coords into the AO atlas

layout(location = 0) out vec4 fragColor;

#include "SceneLight/Lights.glsl"
#include "SceneLight/MapLight.glsl"
#include "SceneAmbientOcclusion.glsl"
#include "SceneLight/ModelShadow.glsl"

// Oren-Nayar diffuse BRDF
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

// GGX microfacet distribution
float GGXDistribution(float m, float dotHalf) {
	float m2 = m * m;
	float t = dotHalf * dotHalf * (m2 - 1.0) + 1.0;
	return m2 / (3.141592653 * t * t);
}

// Cook-Torrance specular BRDF
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
	// Evaluate map shadow
	float shadow = MapShadowVisibility(shadowCoord);
	shadow *= ModelShadowVisibilityAt(worldPosition); // GL: VisibilityOfSunLight = map * model, as the basic shader does

	// The map's and the sky's light, through the face's own ambient occlusion
	// (GL BasicBlockPhys.fs EvaluateAmbientLight)
	vec3 nrm = normalize(normalVarying);
	float detailAO = texture(ambientOcclusionAtlas, ambientOcclusionCoord).x;

	const float ssao = SceneAmbientOcclusion();
	fragColor = vec4(color.xyz, 1.0);
	vec3 diffuseShading = MapAmbientLight(radiosityTextureCoord, aoCoord, nrm, detailAO, ssao);
	float shadowing = shadow * (0.6 * sceneSunSky.sunlight);

	vec3 eyeVec = -normalize(viewSpaceCoord);

	// Compute view-space light direction (sun from the renderer, GetSunDirection)
	vec3 sunDir = normalize(sceneView.sunDirection.xyz);
	vec3 viewSpaceLight = normalize((sceneView.view * vec4(sunDir, 0.0)).xyz);

	float dotNL = max(color.w, 0.001);
	float dotNV = max(dot(viewSpaceNormal, eyeVec), 0.001);

	// Fresnel term
	float fresnel2 = 1.0 - dotNV;
	float fresnel = 0.03 + 0.1 * fresnel2 * fresnel2;

	// What the map and the sky reflect towards the eye
	// (GL BasicBlockPhys.fs EvaluateDirectionalAmbientLight)
	vec3 reflectWS = normalize(reflectionDir);
	vec3 specularShading =
	  MapSoftReflections(radiosityTextureCoord, aoCoord, nrm, reflectWS, detailAO, ssao);

	// Sun diffuse/specular
	if (shadowing > 0.0 && dotNL > 0.0) {
		float sunDiffuseShading = OrenNayar(0.8, dotNL, dotNV);
		diffuseShading += sunDiffuseShading * shadowing;

		float sunSpecularShading = CookTorrance(eyeVec, viewSpaceLight, viewSpaceNormal);
		fragColor.xyz += sunSpecularShading * shadowing;
	}

	// Blend diffuse and specular with Fresnel
	fragColor.xyz = mix(diffuseShading * fragColor.xyz, specularShading, fresnel);
	fragColor.xyz += color.xyz * EvaluateDynamicLights(worldPosition, nrm);

	// Apply fog
	fragColor.xyz = mix(fragColor.xyz, inFogColor, fogDensity);
	fragColor.xyz = max(fragColor.xyz, 0.0);
}
