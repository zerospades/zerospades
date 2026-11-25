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

// DoF circle-of-confusion generation fragment shader.
// Port of OpenGL/PostFilters/DoFCoCGen.fs/.vs.
//
// Samples a 4x4 grid of depth values around the current pixel, converts each
// to a CoC radius, averages them, then adds vignette and global blur.
//
// Descriptor set layout (set 0):
//   binding 0 — depthTexture (sampler2D, SHADER_READ_ONLY depth aspect)

#version 450

layout(binding = 0) uniform sampler2D depthTexture;

layout(push_constant) uniform Params {
    vec2  pixelShift;       // 1/screenWidth, 1/screenHeight
    vec2  zNearFar;         // near, far clip planes
    float depthScale;       // 1.0 / blurDepthRange
    float maxVignetteBlur;  // sin(fovMax/2) * vignetteBlur
    vec2  vignetteScale;    // per-axis vignette UV scaling
    float globalBlur;       // global blur amount
    float nearBlur;         // near blur multiplier
    float farBlur;          // far blur multiplier (already negated on CPU)
    float _pad;
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out float outCoC;

float decodeDepth(float w) {
    return pc.zNearFar.y * pc.zNearFar.x / mix(pc.zNearFar.y, pc.zNearFar.x, w);
}

float CoCAt(vec2 pt) {
    float w     = texture(depthTexture, pt).r;
    float depth = decodeDepth(w);
    float blur  = 1.0 - depth * pc.depthScale;
    return blur * (blur > 0.0 ? pc.nearBlur : pc.farBlur);
}

void main() {
    // Sample 4x4 grid starting at texCoord - 1.5 * pixelShift (mirrors GL VS offset).
    vec2 base = texCoord - pc.pixelShift * 1.5;

    float val = 0.0;
    val += CoCAt(base + pc.pixelShift * vec2(0.0, 0.0));
    val += CoCAt(base + pc.pixelShift * vec2(1.0, 0.0));
    val += CoCAt(base + pc.pixelShift * vec2(2.0, 0.0));
    val += CoCAt(base + pc.pixelShift * vec2(3.0, 0.0));
    val += CoCAt(base + pc.pixelShift * vec2(0.0, 1.0));
    val += CoCAt(base + pc.pixelShift * vec2(1.0, 1.0));
    val += CoCAt(base + pc.pixelShift * vec2(2.0, 1.0));
    val += CoCAt(base + pc.pixelShift * vec2(3.0, 1.0));
    val += CoCAt(base + pc.pixelShift * vec2(0.0, 2.0));
    val += CoCAt(base + pc.pixelShift * vec2(1.0, 2.0));
    val += CoCAt(base + pc.pixelShift * vec2(2.0, 2.0));
    val += CoCAt(base + pc.pixelShift * vec2(3.0, 2.0));
    val += CoCAt(base + pc.pixelShift * vec2(0.0, 3.0));
    val += CoCAt(base + pc.pixelShift * vec2(1.0, 3.0));
    val += CoCAt(base + pc.pixelShift * vec2(2.0, 3.0));
    val += CoCAt(base + pc.pixelShift * vec2(3.0, 3.0));
    val *= (1.0 / 16.0);

    // Vignette (uses actual pixel UV).
    float sq  = length((texCoord - 0.5) * pc.vignetteScale);
    float sq2 = sq * sq * pc.maxVignetteBlur;
    val += sq2;

    // Don't blur the center.
    float scl = min(1.0, sq * 10.0);
    val *= scl;

    outCoC = clamp(val + pc.globalBlur, 0.0, 1.0);
}
