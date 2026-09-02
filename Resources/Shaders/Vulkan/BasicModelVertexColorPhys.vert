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
	mat4 modelMatrix;
	vec3 modelOrigin;
	float fogDensity;
	vec3 customColor;
	float _pad;
	vec3 fogColor;
	float mirrorClipZ; // water-plane Z in the reflection pass (else +inf)
	vec3 sunDirection; // points toward the sun (renderer GetSunDirection)
	float _pad3;
	mat4 viewMatrix;
	vec3 viewOrigin;
} pushConstants;

layout(location = 0) in uvec3 positionAttribute;
layout(location = 1) in uvec3 colorAttribute;
layout(location = 2) in ivec3 normalAttribute;

layout(location = 0) out vec4 color;           // xyz = vertexColor, w = sun lambert (may be negative)
layout(location = 1) out vec3 ambientLight;    // hemisphere ambient fallback
layout(location = 2) out vec3 customColorOut;
layout(location = 3) out vec3 shadowCoord;
layout(location = 4) out vec3 fogDensityOut;
layout(location = 5) out vec3 outFogColor;
layout(location = 6) out vec3 viewSpaceCoord;
layout(location = 7) out vec3 viewSpaceNormal;
layout(location = 8) out vec3 reflectionDir;
layout(location = 9) out vec3 aoCoord;          // 3D coords into AO texture
layout(location = 10) out vec3 radiosityTextureCoord;
layout(location = 11) out vec3 normalVarying;
layout(location = 12) out float waterClip;     // >=0 keep, <0 clip below the reflection plane

// Must match ModelDynamicLit.vert's gl_Position exactly: the additive dynamic-
// light pass uses depth test EQUAL against this physical-lighting opaque pass,
// so a mix of invariant/non-invariant position makes the weapon speckle.
invariant gl_Position;

void main() {
	vec3 position = vec3(positionAttribute);
	vec4 localPos = vec4(position + pushConstants.modelOrigin, 1.0);
	gl_Position = pushConstants.projectionViewMatrix * localPos;

	// World position via model matrix
	vec3 worldPos = (pushConstants.modelMatrix * localPos).xyz;

	vec3 normalFloat = normalize(vec3(normalAttribute));

	// Transform normal to world space
	vec3 worldNormal = normalize((pushConstants.modelMatrix * vec4(normalFloat, 0.0)).xyz);

	// Sun direction from the renderer (single source of truth, GetSunDirection)
	vec3 sunDir = normalize(pushConstants.sunDirection);
	float lambert = dot(worldNormal, sunDir); // NOT clamped

	// Ambient color matching GL GLShadowShader: fog * 0.5 with a minimum
	// luminance floor of 0.35 so things stay visible even when the sky is
	// near-black. (fogColor is already linearized in C++.)
	float hemisphere = 1.0 - worldNormal.z * 0.2;
	vec3 ac = pushConstants.fogColor * 0.5;
	float L = (ac.x + ac.y + ac.z) / 3.0;
	ac += ((ac + 0.003) / (L + 0.003)) * max(0.35 - L, 0.0);
	ambientLight = ac * hemisphere;

	// Vertex color + lambert
	vec3 vertexColor = vec3(colorAttribute) / 255.0;
	color = vec4(vertexColor, lambert);
	customColorOut = pushConstants.customColor;

	// Shadow coordinates
	// Sample slightly inside the surface to avoid shadow bleed at voxel boundaries
	vec3 shadowPos = worldPos - worldNormal * 0.05;
	shadowCoord = vec3(shadowPos.x / 512.0, (shadowPos.y - shadowPos.z) / 512.0, shadowPos.z / 255.0);

	// Fog
	fogDensityOut = vec3(pushConstants.fogDensity);
	outFogColor = pushConstants.fogColor;

	// View-space data for physical lighting
	vec4 viewModelPos = pushConstants.viewMatrix * pushConstants.modelMatrix * localPos;
	viewSpaceCoord = viewModelPos.xyz;
	viewSpaceNormal = normalize((pushConstants.viewMatrix * vec4(worldNormal, 0.0)).xyz);
	reflectionDir = reflect(worldPos - pushConstants.viewOrigin, worldNormal);

	aoCoord = (worldPos + vec3(0.0, 0.0, 1.0)) / vec3(512.0, 512.0, 65.0);

	// Radiosity 3D-texture coords (matches GL MapRadiosity.vs).
	radiosityTextureCoord = worldPos / vec3(512.0, 512.0, 64.0);
	normalVarying = worldNormal;

	// Reflection-pass water clip: negative below the water plane.
	// mirrorClipZ is +inf in the normal scene pass, so this stays positive.
	waterClip = pushConstants.mirrorClipZ - worldPos.z;
}
