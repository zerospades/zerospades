/*
 Copyright (c) 2026 Fran6nd

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

// Screen-space cavity / silhouette outline.
//
// For every pixel we sample the depth buffer at the centre and four
// cardinal neighbours, linearise each value to view-space distance,
// and run a 5-tap discrete Laplacian:
//
//     L = | 4·zC − (zR + zL + zU + zD) |
//
// On a smooth surface (flat OR slanted), each pair of opposite
// neighbours averages to zC, so L collapses to ~0. At a silhouette
// the centre lies on one side of a depth jump and L spikes to the
// jump magnitude. We normalise L by zC (relative threshold) so a
// 5 cm step at 1 m and a 5 m step at 100 m count the same, and feed
// the ratio through a smoothstep gate to produce the edge alpha.
//
// We deliberately do NOT reconstruct world position here. Inverse
// projection blows up near the far plane: at a silhouette where one
// neighbour is sky (z ≈ 1.0), the reconstructed world position
// approaches infinity, dominates the tangent baseline, and crushes
// the edge ratio to zero. Linearised depth + Laplacian sidesteps the
// problem entirely and is cheap.
//
// Distance fade uses the centre tap's linear depth against
// `fogDistance`, so the filter does not depend on the fog filter.

#version 450

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D colorTexture;
layout(set = 0, binding = 1) uniform sampler2D depthTexture;

layout(push_constant) uniform Params {
    vec4 invViewport;            // xy = 1/screen, zw reserved
    vec4 zNearFarFogStrength;    // x = zNear, y = zFar, z = fogDistance, w = strength
    vec4 thresholds;             // x = relative threshold (fraction of centre depth)
} pc;

float linearise(float z) {
    float n = pc.zNearFarFogStrength.x;
    float f = pc.zNearFarFogStrength.y;
    // Vulkan: ndc.z = z ∈ [0,1]. Inverse of the standard z-mapping.
    return (n * f) / (f - z * (f - n));
}

void main() {
    vec3 inputColor = texture(colorTexture, texCoord).rgb;

    vec2 invRes = pc.invViewport.xy;
    float fogDistance = pc.zNearFarFogStrength.z;
    float strength = pc.zNearFarFogStrength.w;
    float threshold = pc.thresholds.x;

    float zC = linearise(texture(depthTexture, texCoord).r);
    float zR = linearise(texture(depthTexture, texCoord + vec2(invRes.x, 0.0)).r);
    float zL = linearise(texture(depthTexture, texCoord - vec2(invRes.x, 0.0)).r);
    float zU = linearise(texture(depthTexture, texCoord + vec2(0.0, invRes.y)).r);
    float zD = linearise(texture(depthTexture, texCoord - vec2(0.0, invRes.y)).r);

    float laplacian = abs(4.0 * zC - zR - zL - zU - zD);

    // Normalise the Laplacian by the closest tap, not the centre. At a
    // silhouette against the sky, the centre on the sky side sits at the
    // far plane (z ≈ 1, linearised ≈ zFar) and its `distanceFade` would
    // otherwise crush the alpha to zero — making outlines disappear on the
    // sky side of every voxel boundary that meets the sky. Using the
    // nearest tap makes the edge belong to the foreground surface that
    // owns it, so both sides of the silhouette paint identically.
    float zNearest = min(min(min(zC, zR), min(zL, zU)), zD);
    float ratio = laplacian / max(zNearest, 0.01);

    float edge = smoothstep(threshold, threshold * 4.0, ratio);

    float distanceFade = 1.0 - smoothstep(fogDistance * 0.6, fogDistance, zNearest);

    float alpha = clamp(edge * distanceFade * strength, 0.0, 1.0);

    fragColor = vec4(mix(inputColor, vec3(0.0), alpha), 1.0);
}
