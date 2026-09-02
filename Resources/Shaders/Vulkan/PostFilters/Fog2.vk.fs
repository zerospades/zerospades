/*
 Copyright (c) 2021 Fran6nd

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

// Fog2 filter fragment shader.
// Port of OpenGL/PostFilters/Fog2.fs.
// Raymarches 16 samples along the view ray and accumulates in-scattered
// sunlight, ambient (per-block AO) and indirect bounce (radiosity) light.
//
// Differences from the GL version that still apply:
//   - No separate dither/noise textures; per-frame noise is generated
//     analytically from gl_FragCoord and pc.ditherFrame.
//   - The Vulkan radiosity backend always uses the high-precision
//     A2R10G10B10_UNORM_PACK32 format, so DecodeRadiosityValue uses
//     the GL `r_radiosity >= 2` (linear) decode branch only.
//
// Descriptor set layout (set 0):
//   binding 0 — colorTexture         (sampler2D)
//   binding 1 — depthTexture         (sampler2D, depth aspect, SHADER_READ_ONLY)
//   binding 2 — shadowMapTexture     (sampler2D)
//   binding 3 — ambientShadowTexture (sampler3D, R32G32_SFLOAT)
//   binding 4 — radiosityTextureFlat (sampler3D, A2R10G10B10_UNORM_PACK32)
//   binding 5 — radiosityTextureX    (sampler3D)
//   binding 6 — radiosityTextureY    (sampler3D)
//   binding 7 — radiosityTextureZ    (sampler3D)

#version 450

layout(binding = 0) uniform sampler2D colorTexture;
layout(binding = 1) uniform sampler2D depthTexture;
layout(binding = 2) uniform sampler2D shadowMapTexture;
layout(binding = 3) uniform sampler3D ambientShadowTexture;
layout(binding = 4) uniform sampler3D radiosityTextureFlat;
layout(binding = 5) uniform sampler3D radiosityTextureX;
layout(binding = 6) uniform sampler3D radiosityTextureY;
layout(binding = 7) uniform sampler3D radiosityTextureZ;

layout(push_constant) uniform Params {
    mat4 viewProjectionMatrixInv; // [0..63]   UV → view-centric world
    vec4 viewOriginFogDist;       // [64..79]  xyz=viewOrigin, w=fogDistance
    vec4 sunlightScale;           // [80..95]  xyz
    vec4 ambientScale;            // [96..111] xyz
    vec4 radiosityScale;          // [112..127] xyz
    vec4 ditherFrame;             // [128..143] xy=per-frame noise seed
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 1) in  vec4 viewcentricWorldPositionPartial;
layout(location = 0) out vec4 outColor;

vec3 transformToShadow(vec3 v) {
    v.y -= v.z;
    v *= vec3(1.0 / 512.0, 1.0 / 512.0, 1.0 / 255.0);
    return v;
}

// Linear (10-10-10-2) decode. Vulkan port stores radiosity values in
// A2R10G10B10_UNORM_PACK32 always — no low-precision branch needed.
vec3 DecodeRadiosityValue(vec3 val) {
    val *= 1023.0 / 1022.0;
    val = (val * 2.0) - 1.0;
    return val;
}

void main() {
    vec3  viewOrigin  = pc.viewOriginFogDist.xyz;
    float fogDistance = pc.viewOriginFogDist.w;

    // Reconstruct view-centric world position from hardware depth.
    float localClipZ = texture(depthTexture, texCoord).r;
    vec4  worldPos   = viewcentricWorldPositionPartial
                     + pc.viewProjectionMatrixInv * vec4(0.0, 0.0, localClipZ, 0.0);
    worldPos.xyz /= worldPos.w;

    // Clip the ray to fogDistance (VOXLAP cylindrical fog model).
    float voxlapDistanceSq = dot(worldPos.xy, worldPos.xy);
    worldPos /= max(sqrt(voxlapDistanceSq) / fogDistance, 1.0);
    voxlapDistanceSq = min(voxlapDistanceSq, fogDistance * fogDistance);

    float goalFogFactor = min(voxlapDistanceSq / (fogDistance * fogDistance), 1.0);
    // Sky pixels get full fog.
    if (localClipZ >= (1.0 - 1e-6))
        goalFogFactor = 1.0;

    // 16-sample raymarching.
    const int numSamples = 16;
    float weightDelta = sqrt(voxlapDistanceSq) / fogDistance / float(numSamples);
    float weightSum   = 0.0;

    // Analytical per-pixel temporal noise (replaces dither + noise textures).
    vec2  seed   = gl_FragCoord.xy + pc.ditherFrame.xy * vec2(127.0, 113.0);
    float dither = fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);

    float weight = 1.0 - weightDelta * dither;

    // Shadow map sampling.
    vec3 currentShadowPos = transformToShadow(viewOrigin);
    vec3 shadowDelta      = transformToShadow(worldPos.xyz / float(numSamples));
    currentShadowPos     += shadowDelta * dither;

    // Radiosity 3D-texture coords (matches GL MapRadiosity.vs).
    vec3 currentRadCoord = viewOrigin / vec3(512.0, 512.0, 64.0);
    vec3 radCoordDelta   = (worldPos.xyz / float(numSamples)) / vec3(512.0, 512.0, 64.0);
    currentRadCoord     += radCoordDelta * dither;

    // Ambient-shadow 3D-texture coords (matches GL Fog2.fs and BasicMap.vert).
    vec3 currentAOCoord = (viewOrigin + vec3(0.0, 0.0, 1.0)) / vec3(512.0, 512.0, 65.0);
    vec3 aoCoordDelta   = (worldPos.xyz / float(numSamples)) / vec3(512.0, 512.0, 65.0);
    // Above z=0 (sky-bound rays), the radiosity texture has no data, so fade
    // its contribution out using the same z-cutoff as GL Fog2.fs.
    float radCutoff       = currentAOCoord.z * 10.0 + 1.0;
    float radCutoffDelta  = aoCoordDelta.z * 10.0;
    currentAOCoord       += aoCoordDelta * dither;

    float sunlightFactor   = 0.0;
    float ambientFactor    = 0.0;
    vec3  radiosityFactor  = vec3(0.0);

    for (int i = 0; i < numSamples; ++i) {
        // Sunlight shadow.
        float shadowHeight = texture(shadowMapTexture, currentShadowPos.xy).w;
        float lit          = step(currentShadowPos.z, shadowHeight);
        sunlightFactor += lit * weight;

        // Per-block ambient occlusion: .x = AO sample, .y = sample weight.
        float aoSample = max(texture(ambientShadowTexture, currentAOCoord).x, 0.0);
        ambientFactor += aoSample * weight;

        // Indirect radiosity bounce.
        vec3 r = DecodeRadiosityValue(texture(radiosityTextureFlat, currentRadCoord).xyz);
        radiosityFactor += r * (weight * clamp(radCutoff, 0.0, 1.0));

        currentShadowPos += shadowDelta;
        currentRadCoord  += radCoordDelta;
        currentAOCoord   += aoCoordDelta;
        radCutoff        += radCutoffDelta;
        weightSum        += weight;
        weight           -= weightDelta;
    }

    // Rescale to the desired fog density.
    vec3 scale            = vec3(goalFogFactor) / (weightSum + 1.0e-4);
    vec3 sunlightContrib  = sunlightFactor   * pc.sunlightScale.xyz  * scale;
    vec3 ambientContrib   = ambientFactor    * pc.ambientScale.xyz   * scale;
    vec3 radiosityContrib = radiosityFactor                          * scale
                          * pc.radiosityScale.xyz;

    // Directional brightness gradient. Sun direction packed into the free
    // .w slots of the scale vectors (keeps push constants at 144 bytes).
    vec3  sunDir = normalize(vec3(pc.sunlightScale.w, pc.ambientScale.w, pc.radiosityScale.w));
    float bright = dot(sunDir, normalize(worldPos.xyz));
    sunlightContrib  *= bright * 0.5 + 1.0;
    ambientContrib   *= bright * 0.5 + 1.0;
    // (Radiosity already encodes direction; no gradient term in GL Fog2 either.)

    outColor      = texture(colorTexture, texCoord);
    outColor.xyz += sunlightContrib + ambientContrib + radiosityContrib;
}
