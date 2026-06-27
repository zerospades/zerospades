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

// Port of OpenGL/PostFilters/ColorCorrection.fs.
//
// GL applies, in this order on a sRGB-encoded input:
//   1. tint multiply  (white-balance — cancels fog cast)
//   2. saturation desat (mix toward gray)
//   3. (HDR only) linearize → ACES filmic tonemap → delinearize
//   4. enhancement smoothstep
// All operations happen in encoded (perceptual) space; the HDR linearize
// roundtrip is the only deviation from that. Critically, ACES is GATED
// on the `USE_HDR` define — applying it in non-HDR mode shifts blues to
// purple and reds toward orange, since ACES is calibrated for true HDR.
//
// Vulkan input is LINEAR (the scene shaders write linear values to the
// UNORM/A2B10G10R10 offscreen buffer, and the swapchain blit does the
// sRGB encoding for display). We sqrt the input first to recover GL's
// "operates on encoded values" semantics, then square back at the end.

#version 450

layout(binding = 0) uniform sampler2D sceneTexture;

layout(push_constant) uniform Params {
    vec4 tintAndEnhancement;  // xyz = tint, w = enhancement
    vec4 satAndHdr;           // x = saturation, y = useHdr (0 or 1), z/w = unused
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

vec3 acesToneMapping(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 color = texture(sceneTexture, texCoord).rgb;
    color = max(color, 0.0);

    // Move to encoded (perceptual) space so tint and saturation match GL.
    color = sqrt(color);

    // White-balance tint (cancels the fog colour cast).
    color *= pc.tintAndEnhancement.xyz;

    // Saturation: mix toward perceptual gray.
    vec3 gray = vec3(dot(color, vec3(1.0 / 3.0)));
    float saturation = pc.satAndHdr.x;
    color = mix(gray, color, saturation);

    // ACES tonemap only when r_hdr is on. In non-HDR mode the input is
    // already a properly-balanced [0, 1] image and ACES would shift its
    // primaries (notably crushing blues toward purple).
    if (pc.satAndHdr.y > 0.5) {
        vec3 lin = color * color;          // encoded → linear for ACES
        lin = acesToneMapping(lin * 0.8);
        color = sqrt(lin);                 // back to encoded
    }

    // Enhancement smoothstep — operates on encoded values per GL.
    float enhancement = pc.tintAndEnhancement.w;
    color = mix(color, smoothstep(0.0, 1.0, color), enhancement);

    // Output linear; sRGB swapchain blit will encode for display.
    vec3 outRgb = color * color;

    // Guard against NaN/Inf reaching the swapchain. A stray NaN from an earlier
    // shader (e.g. a divide-by-zero at a silhouette) would otherwise display as a
    // garbage magenta texel — very visible on high-contrast edges when no post-AA
    // smears it away. isnan/isinf both map such values back to black, and the
    // clamp keeps negatives/overflow in range.
    outRgb = mix(outRgb, vec3(0.0), vec3(isnan(outRgb)) + vec3(isinf(outRgb)));
    outColor = vec4(max(outRgb, vec3(0.0)), 1.0);
}
