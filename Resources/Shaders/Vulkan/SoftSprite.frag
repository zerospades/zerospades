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
	vec3 rightVector;
	float _pad1;
	vec3 upVector;
	float _pad2;
	vec3 frontVector;
	float _pad3;
	vec3 viewOriginVector;
	float _pad4;
	vec3 fogColor;
	float fogDistance;
	vec2 zNearFar;
} pc;

layout(set = 0, binding = 0) uniform sampler2D mainTexture;
layout(set = 1, binding = 0) uniform sampler2D depthTexture;

layout(location = 0) in vec4 color;
layout(location = 1) in vec4 texCoord;
layout(location = 2) in vec4 fogDensity;
layout(location = 3) in vec4 depthRange;

layout(location = 0) out vec4 fragColor;

float decodeDepth(float w, float near, float far) {
	return 1.0 / mix(far, near, w);
}

float depthAt(vec2 pt) {
	float w = texture(depthTexture, pt).x;
	return decodeDepth(w, pc.zNearFar.x, pc.zNearFar.y);
}

void main() {
	// Get scene depth at this fragment
	float depth = depthAt(texCoord.zw);
	if (depth < depthRange.y)
		discard;

	fragColor = texture(mainTexture, texCoord.xy);

	// Premultiplied alpha
	fragColor.xyz *= fragColor.w;
	fragColor *= color;

	// Apply fog
	vec4 fogColorP = vec4(pc.fogColor, 1.0);
	fogColorP *= fragColor.w; // Premultiplied alpha
	fragColor = mix(fragColor, fogColorP, fogDensity);

	// Soft particle fade at geometry edges
	float soft = depth * depthRange.z + depthRange.x;
	soft = smoothstep(0.0, 1.0, soft);
	fragColor *= soft;

	// Discard nearly transparent fragments
	if (dot(fragColor, vec4(1.0)) < 0.002)
		discard;
}
