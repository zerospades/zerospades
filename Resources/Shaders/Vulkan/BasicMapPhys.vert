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
	float _pad3;
	mat4 viewMatrix;
} pushConstants;

layout(location = 0) in uvec3 positionAttribute;
layout(location = 1) in uvec2 aoCoordAttribute;
layout(location = 2) in uvec3 colorAttribute;
layout(location = 3) in ivec3 normalAttribute;
layout(location = 4) in ivec3 fixedPositionAttribute; // face-center * 2 (chunk-local)

layout(location = 0) out vec4 color;           // xyz = linearized vertex color, w = sun lambert (may be negative)
layout(location = 1) out vec3 ambientLight;    // hemisphere ambient fallback
layout(location = 2) out vec3 fogDensity;
layout(location = 3) out vec3 outFogColor;
layout(location = 4) out vec3 shadowCoord;
layout(location = 5) out vec3 viewSpaceCoord;
layout(location = 6) out vec3 viewSpaceNormal;
layout(location = 7) out vec3 reflectionDir;
layout(location = 8) out vec3 aoCoord;          // 3D coords into AO texture
layout(location = 9) out vec3 radiosityTextureCoord; // 3D coords into radiosity textures
layout(location = 10) out vec3 normalVarying;   // world-space surface normal

void main() {
	vec3 position = vec3(positionAttribute);
	vec4 worldPos = vec4(position + pushConstants.modelOrigin, 1.0);
	gl_Position = pushConstants.projectionViewMatrix * worldPos;

	vec3 normalFloat = normalize(vec3(normalAttribute));

	// Sun direction from the renderer (single source of truth, GetSunDirection)
	vec3 sunDir = normalize(pushConstants.sunDirection);
	float lambert = dot(normalFloat, sunDir); // NOT clamped - physical shader needs raw dot

	// Linearize vertex color
	vec3 vertexColor = vec3(colorAttribute) / 255.0;
	vertexColor *= vertexColor;

	// Ambient color matching GL GLShadowShader: fog * 0.5 with a minimum
	// luminance floor of 0.35 so things stay visible even when the sky is
	// near-black. (fogColor is already linearized in C++.)
	float hemisphere = 1.0 - normalFloat.z * 0.2;
	vec3 ac = pushConstants.fogColor * 0.5;
	float L = (ac.x + ac.y + ac.z) / 3.0;
	ac += ((ac + 0.003) / (L + 0.003)) * max(0.35 - L, 0.0);
	ambientLight = ac * hemisphere;

	color = vec4(vertexColor, lambert);

	// Shadow coordinates — sample at the face center ("fixed position",
	// matches GL fixedPositionAttribute) so voxel-boundary vertices don't
	// leak the neighboring column's shadow value (lit sliver on face edges).
	vec3 wPos = vec3(fixedPositionAttribute) * 0.5 + pushConstants.modelOrigin;
	shadowCoord = vec3(wPos.x / 512.0, (wPos.y - wPos.z) / 512.0, wPos.z / 255.0);

	// Fog
	vec2 horzRelativePos = worldPos.xy - pushConstants.viewOrigin.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	fogDensity = vec3(min(horzDistance / (pushConstants.fogDistance * pushConstants.fogDistance), 1.0));
	outFogColor = pushConstants.fogColor;

	// View-space data for physical lighting
	viewSpaceCoord = (pushConstants.viewMatrix * worldPos).xyz;
	viewSpaceNormal = normalize((pushConstants.viewMatrix * vec4(normalFloat, 0.0)).xyz);
	reflectionDir = reflect(worldPos.xyz - pushConstants.viewOrigin, normalFloat);

	aoCoord = (wPos + vec3(0.0, 0.0, 1.0)) / vec3(512.0, 512.0, 65.0);

	// Radiosity 3D-texture coords (matches GL MapRadiosity.vs).
	radiosityTextureCoord = wPos / vec3(512.0, 512.0, 64.0);
	normalVarying = normalFloat;
}
