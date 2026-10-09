/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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

// Lens flare visibility scanner — fragment shader.
//
// Port of Shaders/OpenGL/LensFlare/Scanner.fs.
//
// Compares scanPos.z against the offscreen depth texture, done in-shader
// rather than via a sampler2DShadow hardware compare: the depth texture
// this samples is an R32F color image (CopySceneDepthForSampling / the
// MSAA depth-resolve pass copy the D32 depth attachment into an R32F
// color target, since MoltenVK maps GLSL sampler2D to Metal's
// texture2d<float>, which cannot read a D32/depth2d<float> image — see
// VulkanFramebufferManager::sceneDepthSampleImage). Hardware compare is
// invalid on a color format regardless, so the compare must be manual.
// Softness comes from the 3x Gauss blur applied afterwards.
//
// A radial mask trims the disc inside circlePos (radius = 32 in NDC
// pixels).

#version 450

layout(binding = 0) uniform sampler2D depthTexture;

layout(location = 0) in vec3 scanPos;
layout(location = 1) in vec2 circlePos;

layout(location = 0) out vec4 outColor;

// Bilinearly-filtered depth compare, i.e. what GL gets for free from a
// CompareRefToTexture sampler set to Linear (hardware 2x2 PCF).
//
// Comparing a single nearest tap instead makes visibility binary per texel:
// when the flare sits on a depth discontinuity, sub-pixel camera movement
// flips whole texels between 0 and 1, the visibility buffer jumps, and the
// sun's halo pops to full brightness for a frame -- a full-screen white
// blink while looking near an occluded sun. Filtering the *comparison
// results* (not the depths, which must never be averaged across a
// discontinuity) restores GL's smooth fractional visibility.
float CompareFiltered(vec2 uv, float ref) {
    vec2 texSize = vec2(textureSize(depthTexture, 0));
    vec2 texel = 1.0 / texSize;

    // Snap to the 2x2 texel neighbourhood the hardware would blend.
    vec2 coord = uv * texSize - 0.5;
    vec2 frac = fract(coord);
    vec2 base = (floor(coord) + 0.5) * texel;

    float d00 = texture(depthTexture, base).x;
    float d10 = texture(depthTexture, base + vec2(texel.x, 0.0)).x;
    float d01 = texture(depthTexture, base + vec2(0.0, texel.y)).x;
    float d11 = texture(depthTexture, base + texel).x;

    return mix(mix(step(ref, d00), step(ref, d10), frac.x),
               mix(step(ref, d01), step(ref, d11), frac.x), frac.y);
}

void main() {
    float val = CompareFiltered(scanPos.xy, scanPos.z);

    // Circle trim — matches the GL Scanner.fs `radius = 32` parameter.
    float rad = length(circlePos) * 32.0;
    rad = clamp(32.0 - 1.0 - rad, 0.0, 1.0);
    val *= rad;

    outColor = vec4(vec3(val), 1.0);
}
