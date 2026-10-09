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
// no-radiosity permutation (matches GL OptimizedVoxelModel.fs + MapRadiosityNull.fs).
// Driven by r_radiosity at pipeline-creation time.
layout(constant_id = 0) const int USE_RADIOSITY = 0;

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
// World-space position, where the dynamic lights are evaluated
layout(location = 15) in vec3 worldPosition;

layout(location = 0) out vec4 fragColor;

#include "SceneLight/Lights.glsl"
#include "SceneLight/MapLight.glsl"
#include "SceneLight/ModelShadow.glsl"

void main() {
	// Reflection pass: discard fragments below the water plane so underwater
	// players never enter the mirror (waterClip is +inf-based in the scene pass).
	if (waterClip < 0.0)
		discard;

	// Evaluate map shadow (matching OpenGL Map.fs: EvaluateMapShadow)
	float shadow = MapShadowVisibility(shadowCoord);
	shadow *= ModelShadowVisibility(modelShadowCoord0, modelShadowCoord1, modelShadowCoord2,
	                                shadowViewDepth); // model-on-model cascades (set 1)

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
	vec3 sun = vec3(0.6 * sceneSunSky.sunlight) * sunLambert * shadow;

	vec3 diffuse;
	if (USE_RADIOSITY != 0) {
		// MapRadiosity.fs path — 3D radiosity + 3D AO modulating sky-ambient.
		float aoFactor = MapAmbientShadow(aoCoord);

		vec3 radiosity = MapRadiosity(radiosityTextureCoord, nrm);

		// Blend in the per-vertex detail AO from the 2D atlas, as GL does via
		// EvaluateAmbientLight(ao) -> EvaluateRadiosity. Without it the sqrt()
		// lift is missing and ambient-lit faces come out darker than GL.
		float detailAO = texture(ambientOcclusionAtlas, ambientOcclusionCoord).x;
		float amb = mix(sqrt(aoFactor * detailAO), min(aoFactor, detailAO), 0.5);

		float aoTerm = amb * (0.8 - nrm.z * 0.2);
		vec3 ambientColor = sceneSunSky.ambientLight * sceneSunSky.daylight;

		diffuse = radiosity + aoTerm * ambientColor + sun;
	} else {
		// MapRadiosityNull.fs + GL OptimizedVoxelModel.fs path — per-face AO
		// from the 2D atlas (baked per-vertex aoID), modulating
		// mix(fog, white, 0.5) ambient.
		float ao = texture(ambientOcclusionAtlas, ambientOcclusionCoord).x;
		float hemisphere = 1.0 - nrm.z * 0.2;
		vec3 ambientColor = mix(sceneSunSky.skyLight, vec3(1.0), 0.5) * sceneSunSky.daylight;
		diffuse = ambientColor * (0.5 * ao * hemisphere) + sun;
	}

	fragColor = vec4(vertexColor * diffuse, 1.0);
	fragColor.xyz += vertexColor * EvaluateDynamicLights(worldPosition, nrm);

	// Apply fog fading
	fragColor.xyz = mix(fragColor.xyz, inFogColor, fogDensity);
	fragColor.xyz = max(fragColor.xyz, 0.0);
	// Write linear; the swapchain blit (UNORM->SRGB) encodes for display.
}
