/*
 Copyright (c) 2015 Fran6nd

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

// Multiplies the scene colour by the exposure gain stored in the
// persistent 1×1 accumulator, then applies GL's output transfer curve.
//
// The swapchain is VK_FORMAT_B8G8R8A8_SRGB, so the vkCmdBlitImage that
// hands this output to the swapchain always applies the sRGB encode. But
// GL does not use the sRGB curve: GLNonlinearizeFilter applies a plain
// pow(c, 1/r_hdrGamma) power curve. The two agree in the midtones and
// highlights and diverge sharply in the deep shadows, where sRGB's linear
// toe crushes what the power curve lifts — at linear 0.001 the power curve
// gives 0.047 against sRGB's 0.013. That is why Vulkan matched GL at p95
// and p99 while sitting ~14 levels low at p1/p5, and why the deficit was
// worst in whichever channel was darkest.
//
// Writing the result linearly and letting the blit encode it is therefore
// NOT equivalent to GL. Instead apply GL's power curve here and then
// DECODE to linear, so the blit's mandatory sRGB encode restores exactly
// the bytes GL would have written. This is the same round-trip the 2D UI
// path uses in BasicImage.frag, for the same reason.

#version 450

layout(binding = 0) uniform sampler2D sceneTexture;
layout(binding = 1) uniform sampler2D gainTexture;

vec3 srgbToLinear(vec3 c) {
	bvec3 lo = lessThanEqual(c, vec3(0.04045));
	vec3 linLo = c / 12.92;
	vec3 linHi = pow((c + 0.055) / 1.055, vec3(2.4));
	return mix(linHi, linLo, vec3(lo));
}

layout(push_constant) uniform Params {
	float invGamma; // 1.0 / r_hdrGamma, matching GLNonlinearizeFilter
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
	vec3 color = texture(sceneTexture, texCoord).rgb;
	float gain = texture(gainTexture, vec2(0.5, 0.5)).r;
	color = max(color * gain, 0.0);

	// GL's output curve, then undo the sRGB encode the blit will re-apply.
	color = pow(color, vec3(pc.invGamma));
	outColor = vec4(srgbToLinear(color), 1.0);
}
