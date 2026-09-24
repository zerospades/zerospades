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

// Bloom final composite — Vulkan port of GL's LensDust.fs (the real
// r_bloom composite). Linear framebuffer, so GL's linearize/sqrt round
// trip is dropped; the dust texture still gets squared to linearize it
// (it is a plain sRGB-ish JPG).

layout(binding = 0) uniform sampler2D inputTexture;
layout(binding = 1) uniform sampler2D blurTexture1;
layout(binding = 2) uniform sampler2D dustTexture;
layout(binding = 3) uniform sampler2D noiseTexture;

layout(push_constant) uniform LensDustPC {
	vec4 noiseTexCoordFactor;
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
	vec3 dust1 = texture(dustTexture, texCoord).xyz;
	dust1 *= dust1; // linearize

	vec3 blur1 = texture(blurTexture1, texCoord).xyz;

	vec3 sum = dust1 * blur1;

	vec3 final = texture(inputTexture, texCoord).xyz;

	final *= 0.95;
	final += sum * 2.0;

	// film grain
	vec4 noiseTexCoord = texCoord.xyxy * pc.noiseTexCoordFactor;
	float grain = texture(noiseTexture, noiseTexCoord.xy).x;
	grain += texture(noiseTexture, noiseTexCoord.zw).x;
	grain = fract(grain) - 0.5;
	final += grain * 0.003;

	outColor = vec4(max(final, 0.0), 1.0);
}
