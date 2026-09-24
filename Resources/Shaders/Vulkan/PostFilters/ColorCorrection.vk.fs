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
// sRGB encoding for display), so this pass moves into GL's "operates on
// encoded values" space and back out again. Which encoding that is depends
// on what ran before:
//
//   r_hdr off — the input is the raw linear scene and GL's colour buffer
//               held sqrt(linear), so the pair is sqrt / square.
//   r_hdr on  — AutoExposureApply already applied GL's pow(c, 1/r_hdrGamma)
//               output curve and then sRGB-DECODED it, so that the mandatory
//               sRGB encode at the swapchain blit restores GL's bytes. The
//               value arriving here is therefore sRGB-decoded, and the pair
//               must be linearToSrgb / srgbToLinear. Using sqrt there is the
//               wrong inverse: the round trip still cancels, so the overall
//               level survives, but tint, saturation, ACES and the
//               enhancement smoothstep all run on values several percent off
//               GL's in the midtones and ~24% high in deep shadows.

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

vec3 srgbToLinear(vec3 c) {
    bvec3 lo = lessThanEqual(c, vec3(0.04045));
    vec3 linLo = c / 12.92;
    vec3 linHi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(linHi, linLo, vec3(lo));
}

vec3 linearToSrgb(vec3 c) {
    bvec3 lo = lessThanEqual(c, vec3(0.0031308));
    vec3 sLo = c * 12.92;
    vec3 sHi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(sHi, sLo, vec3(lo));
}

void main() {
    vec3 color = texture(sceneTexture, texCoord).rgb;
    color = max(color, 0.0);

    bool useHdr = pc.satAndHdr.y > 0.5;

    // Move to encoded (perceptual) space so tint and saturation match GL.
    // The encoding is whichever one the preceding stage left behind — see
    // the note at the top of this file.
    color = useHdr ? linearToSrgb(color) : sqrt(color);

    // White-balance tint (cancels the fog colour cast).
    color *= pc.tintAndEnhancement.xyz;

    // Saturation: mix toward perceptual gray.
    vec3 gray = vec3(dot(color, vec3(1.0 / 3.0)));
    float saturation = pc.satAndHdr.x;
    color = mix(gray, color, saturation);

    // ACES tonemap only when r_hdr is on. In non-HDR mode the input is
    // already a properly-balanced [0, 1] image and ACES would shift its
    // primaries (notably crushing blues toward purple).
    // GL squares/square-roots around ACES regardless of its own output curve
    // (ColorCorrection.fs), so this inner pair stays sqrt/square in both modes.
    if (useHdr) {
        vec3 lin = color * color;          // encoded → linear for ACES
        lin = acesToneMapping(lin * 0.8);
        color = sqrt(lin);                 // back to encoded
    }

    // Enhancement smoothstep — operates on encoded values per GL.
    float enhancement = pc.tintAndEnhancement.w;
    color = mix(color, smoothstep(0.0, 1.0, color), enhancement);

    // Back to linear; the sRGB swapchain blit encodes for display. This must
    // be the exact inverse of the forward transform above.
    vec3 outRgb = useHdr ? srgbToLinear(color) : color * color;

    // Guard against NaN/Inf reaching the swapchain. A stray NaN from an earlier
    // shader (e.g. a divide-by-zero at a silhouette) would otherwise display as a
    // garbage magenta texel — very visible on high-contrast edges when no post-AA
    // smears it away. isnan/isinf both map such values back to black, and the
    // clamp keeps negatives/overflow in range.
    outRgb = mix(outRgb, vec3(0.0), vec3(isnan(outRgb)) + vec3(isinf(outRgb)));
    outColor = vec4(max(outRgb, vec3(0.0)), 1.0);
}
