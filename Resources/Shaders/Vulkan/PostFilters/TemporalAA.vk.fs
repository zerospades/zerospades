/*
 Copyright (c) 2017 yvt, 2026 ZeroSpades developers.

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

// The temporal AA resolve: GL's TemporalAA.fs (UE4-style), writing the chain's
// image and the history the next frame reads, the blend rate in its alpha.

layout(binding = 0) uniform sampler2D inputTexture;
layout(binding = 1) uniform sampler2D historyTexture;
layout(binding = 2) uniform sampler2D depthTexture;

layout(push_constant) uniform Params {
	// Rows x, y and w of the matrix taking this frame's (texture coordinate,
	// depth) to the last frame's texture coordinate
	vec4 reprojectionX, reprojectionY, reprojectionW;
	// The same of the one taking them to the view-centric world
	vec4 inverseX, inverseY, inverseW;
	vec2 inverseSize;
	float fogDistance;
	float firstPersonDepthEnd;
	// 1 where the history starts over
	float reset;
} pc;

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outHistory;

// YUV-RGB conversion routine from Hyper3D, as GL's
vec3 encodePalYuv(vec3 rgb) {
	return vec3(dot(rgb, vec3(0.299, 0.587, 0.114)),
	            dot(rgb, vec3(-0.14713, -0.28886, 0.436)),
	            dot(rgb, vec3(0.615, -0.51499, -0.10001)));
}

vec3 decodePalYuv(vec3 yuv) {
	return vec3(dot(yuv, vec3(1., 0., 1.13983)), dot(yuv, vec3(1., -0.39465, -0.58060)),
	            dot(yuv, vec3(1., 2.03211, 0.)));
}

vec2 Project(vec4 x, vec4 y, vec4 w, vec4 point) {
	return vec2(dot(x, point), dot(y, point)) / dot(w, point);
}

void main() {
	vec3 in0 = texture(inputTexture, texCoord).xyz;
	if (pc.reset > 0.5) {
		// No history to blend: this frame is it, blended at GL's starting rate
		outColor = vec4(in0, 1.0);
		outHistory = vec4(in0, 0.5);
		return;
	}

	// ------------------------------------------------------------------------
	// Reprojection
	//
	// The nearest depth around the pixel, so that foreground objects keep their
	// antialiasing
	vec2 off = pc.inverseSize;
	float inputZ = texture(depthTexture, texCoord).x;
	for (int y = -1; y <= 1; y++)
		for (int x = -1; x <= 1; x++)
			inputZ = min(inputZ, texture(depthTexture, texCoord + vec2(x, y) * off).x);

	// Where the point was in the last frame. The first-person view's models are
	// fixed to the screen.
	vec4 point = vec4(texCoord, inputZ, 1.0);
	vec2 reprojectedTexCoord = inputZ < pc.firstPersonDepthEnd
	                             ? texCoord
	                             : Project(pc.reprojectionX, pc.reprojectionY,
	                                       pc.reprojectionW, point);

	vec4 lastColor = texture(historyTexture, reprojectedTexCoord);

	// ------------------------------------------------------------------------
	// The approximate fog factor, so that barely-visible objects are not
	// blurred away
	vec2 viewcentric = Project(pc.inverseX, pc.inverseY, pc.inverseW, point);
	float voxlapDistanceSq = min(dot(viewcentric, viewcentric), pc.fogDistance * pc.fogDistance);
	float fogFactor = min(voxlapDistanceSq / (pc.fogDistance * pc.fogDistance), 1.0);

	// ------------------------------------------------------------------------
	vec3 antialiased = lastColor.xyz;
	float mixRate = min(lastColor.w, 0.5);

	antialiased = mix(antialiased, in0, mixRate);

	vec3 in1 = texture(inputTexture, texCoord + vec2(+off.x, 0.0)).xyz;
	vec3 in2 = texture(inputTexture, texCoord + vec2(-off.x, 0.0)).xyz;
	vec3 in3 = texture(inputTexture, texCoord + vec2(0.0, +off.y)).xyz;
	vec3 in4 = texture(inputTexture, texCoord + vec2(0.0, -off.y)).xyz;
	vec3 in5 = texture(inputTexture, texCoord + vec2(+off.x, +off.y)).xyz;
	vec3 in6 = texture(inputTexture, texCoord + vec2(-off.x, +off.y)).xyz;
	vec3 in7 = texture(inputTexture, texCoord + vec2(+off.x, -off.y)).xyz;
	vec3 in8 = texture(inputTexture, texCoord + vec2(-off.x, -off.y)).xyz;

	antialiased = encodePalYuv(antialiased);
	in0 = encodePalYuv(in0);
	in1 = encodePalYuv(in1);
	in2 = encodePalYuv(in2);
	in3 = encodePalYuv(in3);
	in4 = encodePalYuv(in4);
	in5 = encodePalYuv(in5);
	in6 = encodePalYuv(in6);
	in7 = encodePalYuv(in7);
	in8 = encodePalYuv(in8);

	vec3 minColor = min(min(min(in0, in1), min(in2, in3)), in4);
	vec3 maxColor = max(max(max(in0, in1), max(in2, in3)), in4);
	minColor = mix(minColor, min(min(min(in5, in6), min(in7, in8)), minColor), 0.5);
	maxColor = mix(maxColor, max(max(max(in5, in6), max(in7, in8)), maxColor), 0.5);

	vec3 preclamping = antialiased;
	antialiased = clamp(antialiased, minColor, maxColor);

	mixRate = 1.0 / (1.0 / mixRate + 1.0);

	// Increase the mix rate if the prediction is unreliable
	{
		vec3 diff = abs(antialiased - preclamping);
		float clampAmount = max(max(diff.x, diff.y), diff.z);
		mixRate += clampAmount * 8.0;
	}

	// Increase the mix rate if the fog factor is high
	// (Prevents barely-visible objects from being blurred away)
	{
		float contrast = 1.0 - fogFactor;
		const float contrastThreshold = 0.1;
		const float contrastFactor = 2.0;
		mixRate += max(0.0, contrastThreshold - contrast) / contrastThreshold * contrastFactor;
	}

	mixRate = clamp(mixRate, 0.05, 0.5);

	antialiased = max(decodePalYuv(antialiased), vec3(0.0));

	outColor = vec4(antialiased, 1.0);
	outHistory = vec4(antialiased, mixRate);
}
