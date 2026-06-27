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

// Fog filter (Fog1 style) fragment shader.
// Port of OpenGL/PostFilters/Fog.fs.
// Coarse + fine DDA: marches an 8×8-downsampled min/max shadow map, and
// only drops to per-voxel shadow lookups for cells that are neither fully
// lit nor fully shadowed. Same algorithm as the GL Fog.fs USE_COARSE_
// SHADOWMAP path; without it the pure 512-step fine DDA picks up every
// thin pole's shadow shaft over the full fog distance and prints a sharp
// black cross at the sun-direction vanishing point.
//
// Descriptor set layout (set 0):
//   binding 0 — colorTexture            (sampler2D)
//   binding 1 — depthTexture            (sampler2D, depth aspect)
//   binding 2 — shadowMapTexture        (sampler2D, 512×512, .w = depth)
//   binding 3 — coarseShadowMapTexture  (sampler2D, 64×64, .x = min depth, .y = max depth)

#version 450

layout(binding = 0) uniform sampler2D colorTexture;
layout(binding = 1) uniform sampler2D depthTexture;
layout(binding = 2) uniform sampler2D shadowMapTexture;
layout(binding = 3) uniform sampler2D coarseShadowMapTexture;

layout(push_constant) uniform Params {
    vec4 viewOriginPad;  // xyz = viewOrigin
    vec4 viewAxisUp;     // xyz
    vec4 viewAxisSide;   // xyz
    vec4 viewAxisFront;  // xyz
    vec4 fovZNearFar;    // xy = fov (tan half-angle), z = zNear, w = zFar
    vec4 fogColorDist;   // xyz = fogColor (linear), w = fogDistance
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 1) in  vec3 viewTan;
layout(location = 2) in  vec3 viewDir;
layout(location = 3) in  vec3 shadowOrigin;
layout(location = 4) in  vec3 shadowRayDirection;
layout(location = 0) out vec4 outColor;

float decodeDepth(float w, float near, float far) {
    return far * near / mix(far, near, w);
}

float fogDensFunc(float t) {
    return t;
}

void main() {
    float fogDistance = pc.fogColorDist.w;
    float zNear       = pc.fovZNearFar.z;
    float zFar        = pc.fovZNearFar.w;

    float voxelDistanceFactor = length(shadowRayDirection) / length(viewTan);
    voxelDistanceFactor *= length(viewDir.xy) / length(viewDir); // remove vertical fog

    float w              = texture(depthTexture, texCoord).r;
    float screenDepth    = decodeDepth(w, zNear, zFar);
    float screenDistance = screenDepth * length(viewTan);
    screenDistance       = min(screenDistance, fogDistance);
    float screenVoxelDistance = screenDistance * voxelDistanceFactor;

    const vec2  voxels       = vec2(512.0);
    const vec2  voxelSize    = 1.0 / voxels;
    const float coarseLevel    = 8.0;
    const float coarseLevelInv = 1.0 / coarseLevel;
    const vec2  coarseVoxels    = voxels / coarseLevel;
    const vec2  coarseVoxelSize = voxelSize * coarseLevel;

    float fogDistanceTime = fogDistance * voxelDistanceFactor;
    float zMaxTime        = min(screenVoxelDistance, fogDistanceTime);
    float maxTime         = zMaxTime;
    float ceilTime        = 10000000000.0;

    vec3 startPos = shadowOrigin;
    vec3 dir      = shadowRayDirection;

    // Near-vertical rays: voxelDistanceFactor -> 0, so the fog integral is
    // ~zero anyway. Snapping dir.xy (old GL-style guard) made adjacent
    // fragments flip between the real and snapped direction, glitching the
    // view straight down. Skip the march instead.
    if (length(dir.xy) < 0.0001 * length(dir)) {
        outColor = texture(colorTexture, texCoord);
        return;
    }
    if (dir.x == 0.0) dir.x = 0.00001;
    if (dir.y == 0.0) dir.y = 0.00001;
    dir = normalize(dir);

    if (dir.z < -0.000001) {
        ceilTime = -startPos.z / dir.z - 0.0001;
        maxTime  = min(maxTime, ceilTime);
    }

    vec2 dirSign  = sign(dir.xy);
    vec2 dirSign2 = dirSign * 0.5 + 0.5; // 0 or 1

    float time  = 0.0;
    float total = 0.0;

    // Mirror-pass clip: if the camera sits above z = 63 (true z-axis sign
    // flipped via reflected viewMatrix), skip past it before marching.
    if (startPos.z > (63.0 / 255.0) && dir.z < 0.0) {
        time   = ((63.0 / 255.0) - startPos.z) / dir.z;
        total += fogDensFunc(time);
        startPos += time * dir;
    }

    vec3 pos        = startPos + dir * 0.0001;
    vec2 voxelIndex = floor(pos.xy);
    if (pos.xy == voxelIndex) pos += 0.001;

    vec2 timePerVoxel    = 1.0 / dir.xy;
    vec2 timePerVoxelAbs = abs(timePerVoxel);
    vec2 timeToNextVoxel = (voxelIndex + dirSign2 - pos.xy) * timePerVoxel;

    if (ceilTime <= 0.0) {
        total = fogDensFunc(zMaxTime);
    } else {
        // Coarse traversal: walk the 64×64 cell grid, fetching min/max
        // depths for each cell. Skip "all lit" / "all shadow" cells with
        // one branch; drop to the fine DDA only for ambiguous cells.
        vec3 coarseDir = dir * vec3(coarseLevelInv, coarseLevelInv, 1.0);
        vec3 coarsePos = pos * vec3(coarseLevelInv, coarseLevelInv, 1.0);
        vec2 coarseVoxelIndex = floor(coarsePos.xy);
        if (coarsePos.xy == coarseVoxelIndex) coarsePos += 0.0001;

        vec2 coarseTimePerVoxel    = timePerVoxel    * coarseLevel;
        vec2 coarseTimePerVoxelAbs = timePerVoxelAbs * coarseLevel;
        vec2 coarseTimeToNextVoxel = (coarseVoxelIndex + dirSign2 - coarsePos.xy)
                                     * coarseTimePerVoxel;

        const int maxCoarseSteps = 64;
        const int maxFineSteps   = 64;
        for (int ci = 0; ci < maxCoarseSteps; ++ci) {
            // .x = min depth, .y = max depth in the 8×8 cell (0..1).
            vec2 coarseVal = texture(coarseShadowMapTexture,
                                     coarseVoxelIndex * coarseVoxelSize).xy;

            float coarseDiffTime = min(coarseTimeToNextVoxel.x,
                                       coarseTimeToNextVoxel.y);
            float coarseNextTime = min(time + coarseDiffTime, maxTime);
            float limitedDiffTime = coarseNextTime - time;

            // Range of pos.z over this cell.
            vec2 passingZ = vec2(coarsePos.z);
            passingZ.y += limitedDiffTime * coarseDir.z;

            bvec2 stat = bvec2(min(passingZ.x, passingZ.y) > coarseVal.y,
                               max(passingZ.x, passingZ.y) < coarseVal.x);

            vec2 oldCoarseVoxelIndex = coarseVoxelIndex;
            vec3 nextCoarsePos = coarsePos + coarseDiffTime * coarseDir;
            // Advance to next coarse cell.
            if (coarseTimeToNextVoxel.x < coarseTimeToNextVoxel.y) {
                coarseVoxelIndex.x   += dirSign.x;
                coarseTimeToNextVoxel.y -= coarseDiffTime;
                coarseTimeToNextVoxel.x  = coarseTimePerVoxelAbs.x;
            } else {
                coarseVoxelIndex.y   += dirSign.y;
                coarseTimeToNextVoxel.x -= coarseDiffTime;
                coarseTimeToNextVoxel.y  = coarseTimePerVoxelAbs.y;
            }

            if (any(stat)) {
                if (stat.y) {
                    // Always lit cell.
                    float diffDens = fogDensFunc(coarseNextTime) - fogDensFunc(time);
                    total += diffDens;
                }
                // Else: stat.x — always in shadow, contribute nothing.
                time = coarseNextTime;
            } else if (limitedDiffTime < 1.0e-9) {
                time = coarseNextTime;
            } else {
                // Ambiguous coarse cell — drop to per-voxel DDA inside it.
                pos        = coarsePos * vec3(coarseLevel, coarseLevel, 1.0);
                voxelIndex = floor(pos.xy);
                if (pos.xy == voxelIndex) pos += 0.001;
                timeToNextVoxel = (voxelIndex + dirSign2 - pos.xy) * timePerVoxel;

                for (int fi = 0; fi < maxFineSteps; ++fi) {
                    float v = texture(shadowMapTexture, voxelIndex * voxelSize).w;
                    float val = step(pos.z, v);

                    float diffTime = min(timeToNextVoxel.x, timeToNextVoxel.y);
                    if (timeToNextVoxel.x < timeToNextVoxel.y) {
                        voxelIndex.x       += dirSign.x;
                        timeToNextVoxel.y  -= diffTime;
                        timeToNextVoxel.x   = timePerVoxelAbs.x;
                    } else {
                        voxelIndex.y       += dirSign.y;
                        timeToNextVoxel.x  -= diffTime;
                        timeToNextVoxel.y   = timePerVoxelAbs.y;
                    }

                    pos += dir * diffTime;
                    float nextTime  = min(time + diffTime, maxTime);
                    float diffDens  = fogDensFunc(nextTime) - fogDensFunc(time);
                    time            = nextTime;
                    total          += val * diffDens;

                    if (time >= maxTime) break;
                    // Leaving the current coarse cell — hand control back
                    // to the outer coarse traversal so we don't double-
                    // count cells.
                    if (floor((voxelIndex.xy + 0.5) * coarseLevelInv) != oldCoarseVoxelIndex)
                        break;
                }
            }

            if (time >= maxTime) {
                if (coarseNextTime >= ceilTime) {
                    float diffDens = fogDensFunc(zMaxTime) - fogDensFunc(time);
                    total += max(diffDens, 0.0);
                }
                break;
            }

            coarsePos = nextCoarsePos;
        }
    }

    total = mix(total, fogDensFunc(zMaxTime), 0.04);
    total /= fogDensFunc(fogDistanceTime + 1e-10);

    // Sun direction packed into the free .w slots of the view-axis vectors
    // (filled at render time from renderer.GetSunDirection()).
    vec3 sunDir = normalize(vec3(pc.viewOriginPad.w, pc.viewAxisUp.w, pc.viewAxisSide.w));
    float bright = dot(sunDir, normalize(viewDir));
    total *= 0.8 + bright * 0.3;
    bright = exp2(bright * 16.0 - 15.0);
    total *= bright + 1.0;

    outColor      = texture(colorTexture, texCoord);
    outColor.xyz += total * pc.fogColorDist.xyz;
}
