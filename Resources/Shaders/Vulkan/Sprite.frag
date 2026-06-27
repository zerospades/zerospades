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
	mat4 viewMatrix;
	vec3 rightVector;
	vec3 upVector;
	vec3 viewOriginVector;
	vec3 fogColor;
	float fogDistance;
} pc;

layout(binding = 0) uniform sampler2D mainTexture;

layout(location = 0) in vec4 color;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 fogDensity;

layout(location = 0) out vec4 fragColor;

void main() {
	vec4 texColor = texture(mainTexture, texCoord);

	// Premultiplied alpha
	texColor.xyz *= texColor.w;
	texColor *= color;

	// Apply fog
	vec4 fogColorP = vec4(pc.fogColor, 1.0);
	fogColorP *= texColor.w; // Premultiplied alpha
	texColor = mix(texColor, fogColorP, fogDensity);

	// Discard nearly transparent fragments
	if (dot(texColor, vec4(1.0)) < 0.002)
		discard;

	fragColor = texColor;
}
