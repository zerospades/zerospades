/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#version 450

layout(push_constant) uniform PushConstants {
	vec4 drawRange;
	vec3 color;
	// Where in the scene's depth the first-person view's models end
	float firstPersonDepthEnd;
	vec2 sourceCoord;
	vec2 sourceSpread;
	float sourceDepth;
	float zNear;
	float zFar;
	// 1 when the swapchain stores sRGB, which blends in linear light
	float outputIsLinear;
} pc;

layout(set = 0, binding = 0) uniform sampler2D glareTexture;
layout(set = 0, binding = 1) uniform sampler2D depthTexture;

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec2 depthCoord;
// How much of the lamp is in sight
layout(location = 2) in float visibility;

layout(location = 0) out vec4 outColor;

vec3 SrgbToLinear(vec3 c) {
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void main() {
	// Under the first-person view's hands and weapon, which are drawn over the
	// glare like the rest of the screen in front of the eye.
	if (texture(depthTexture, depthCoord).r < pc.firstPersonDepthEnd)
		discard;

	// What GL adds onto its gamma-encoded frame. An sRGB swapchain blends in linear
	// light, so the amount is decoded first: exact over black, the night it dazzles in.
	vec4 image = texture(glareTexture, texCoord);
	vec3 glare = pc.color * image.rgb * (image.a * visibility);
	outColor = vec4(pc.outputIsLinear > 0.5 ? SrgbToLinear(glare) : glare, 0.0);
}
