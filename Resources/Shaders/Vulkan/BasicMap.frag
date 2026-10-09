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

// Selects the radiosity-on permutation (matches GL MapRadiosity.fs) vs the
// no-radiosity permutation (matches GL MapRadiosityNull.fs + BasicBlock.fs).
// Driven by r_radiosity at pipeline-creation time.
layout(constant_id = 0) const int USE_RADIOSITY = 0;

layout(location = 0) in vec4 color;           // xyz = linearized vertex color, w = sun lambert
layout(location = 1) in vec3 ambientLight;     // hemisphere ambient fallback (unused, kept for VS↔FS compat)
layout(location = 2) in vec3 fogDensity;
layout(location = 3) in vec3 inFogColor;
layout(location = 4) in vec3 shadowCoord;      // shadow map coordinates
layout(location = 5) in vec3 aoCoord;          // 3D coords into AO texture
layout(location = 6) in vec3 radiosityTextureCoord; // 3D coords into radiosity textures
layout(location = 7) in vec3 normalVarying;    // surface normal in world space
layout(location = 8) in vec2 ambientOcclusionCoord; // 2D coords into AO atlas
layout(location = 9) in vec3 modelShadowCoord0;     // light-clip coords per cascade
layout(location = 10) in vec3 modelShadowCoord1;
layout(location = 11) in vec3 modelShadowCoord2;
layout(location = 12) in float shadowViewDepth;     // camera-axis depth, for cascade choice
// World-space position, where the dynamic lights are evaluated
layout(location = 13) in vec3 worldPosition;

layout(location = 0) out vec4 fragColor;

#include "SceneLight/Lights.glsl"
#include "SceneLight/MapLight.glsl"
#include "SceneLight/ModelShadow.glsl"

void main() {
	// Map shadow (matches GL Map.fs: EvaluateMapShadow)
	float shadow = MapShadowVisibility(shadowCoord);

	// Fold in dynamic model shadows (GL: VisibilityOfSunLight = map * model).
	shadow *= ModelShadowVisibility(modelShadowCoord0, modelShadowCoord1, modelShadowCoord2,
	                                shadowViewDepth);

	vec3 nrm = normalize(normalVarying);
	vec3 vertexColor = color.xyz;
	float sunLambert = color.w;

	// Sun contribution — matches GL Common.fs EvaluateSunLight() * color.w
	vec3 sun = vec3(0.6 * sceneSunSky.sunlight) * sunLambert * shadow;

	// Per-block ambient occlusion (sampled from 3D ambient shadow texture).
	float aoFactor = MapAmbientShadow(aoCoord);

	vec3 diffuse;
	if (USE_RADIOSITY != 0) {
		// MapRadiosity.fs path — directional radiosity + ambient·skyAmbient.
		vec3 radiosity = MapRadiosity(radiosityTextureCoord, nrm);

		// Blend the coarse 3D ambient-shadow AO with the per-vertex detail AO
		// from the 2D atlas, exactly as GL MapRadiosity.fs EvaluateRadiosity
		// does. Leaving the detail term out (as this port previously did) is
		// not merely "less detail": the sqrt() lifts every value below 1, so
		// the coarse term alone is markedly darker than GL wherever a surface
		// is shadowed but not itself creased -- e.g. the flat underside of an
		// overhang, which is the worst-matching region against GL.
		float detailAO = texture(ambientOcclusionAtlas, ambientOcclusionCoord).x;
		float amb = mix(sqrt(aoFactor * detailAO), min(aoFactor, detailAO), 0.5);

		// GL GLShadowShader's ambient colour, worked out in full daylight
		// (`VulkanRenderer::GetSunSky`), drawn at the daylight.
		float aoTerm = amb * (0.8 - nrm.z * 0.2);
		vec3 ambientColor = sceneSunSky.ambientLight * sceneSunSky.daylight;

		diffuse = radiosity + aoTerm * ambientColor + sun;
	} else {
		// MapRadiosityNull.fs path — ambient = mix(fog, white, 0.5) * 0.5 * ao * hemisphere.
		// Hemisphere is (1 - normal.z * 0.2), not the radiosity path's (0.8 - normal.z * 0.2).
		// AO is sampled from the 2D AmbientOcclusion atlas (per-vertex tile coord),
		// matching GL BasicBlock.fs — gives crisper voxel-corner AO than the
		// 3D ambientShadowTexture used in the radiosity branch.
		float ao = texture(ambientOcclusionAtlas, ambientOcclusionCoord).x;
		float hemisphere = 1.0 - nrm.z * 0.2;
		vec3 ambientColor = mix(sceneSunSky.skyLight, vec3(1.0), 0.5) * sceneSunSky.daylight;
		diffuse = ambientColor * (0.5 * ao * hemisphere) + sun;
	}

	fragColor = vec4(vertexColor * diffuse, 1.0);
	fragColor.xyz += vertexColor * EvaluateDynamicLights(worldPosition, nrm);

	// Fog fade
	fragColor.xyz = mix(fragColor.xyz, inFogColor, fogDensity);
}
