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

layout(binding = 0) uniform sampler2D mainTexture;

// Must match BasicImage.vert exactly: one range covers both stages.
layout(push_constant) uniform PushConstants {
	vec2 invScreenSizeFactored;
	vec2 invTextureSize;
	vec2 clipCircleCenter; // screen pixels
	float clipCircleRadius; // <= 0 disables the circular clip
	float _pad;
} pushConstants;

layout(location = 0) in vec4 color;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;

// The swapchain attachment is VK_FORMAT_B8G8R8A8_SRGB: the hardware
// sRGB-ENCODES whatever this shader writes. But 2D UI textures are
// uploaded as UNORM (raw sRGB bytes sampled verbatim) and the GL
// renderer writes those bytes straight to a non-sRGB framebuffer.
// Without correction the value gets sRGB-encoded a second time ->
// washed-out minimap/UI. Fix: do all math in sRGB space exactly like
// GL, then DECODE to linear so the attachment's encode restores the
// original bytes.
vec3 srgbToLinear(vec3 c) {
	bvec3 lo = lessThanEqual(c, vec3(0.04045));
	vec3 linLo = c / 12.92;
	vec3 linHi = pow((c + 0.055) / 1.055, vec3(2.4));
	return mix(linHi, linLo, vec3(lo));
}

void main() {
	// Circular clip. gl_FragCoord is in framebuffer pixels with a top-left
	// origin, which is exactly the screen coordinate space the client passes
	// in, so the centre needs no conversion.
	if (pushConstants.clipCircleRadius > 0.0) {
		float r = pushConstants.clipCircleRadius;
		if (dot(gl_FragCoord.xy - pushConstants.clipCircleCenter,
		        gl_FragCoord.xy - pushConstants.clipCircleCenter) > r * r)
			discard;
	}

	vec2 flippedTexCoord = vec2(texCoord.x, 1.0 - texCoord.y);
	vec4 col = texture(mainTexture, flippedTexCoord);
	col.xyz *= col.w;
	col *= color;
	fragColor = vec4(srgbToLinear(col.xyz), col.w);
}
