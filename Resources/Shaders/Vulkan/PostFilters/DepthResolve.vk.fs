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

// Depth resolve for MSAA.
//
// vkCmdResolveImage cannot resolve depth attachments, so under MSAA we
// resolve the multisampled depth here with a trivial fullscreen pass: read
// sample 0 of the multisampled depth texture and write the raw [0,1] depth
// value into a single-sample R32_SFLOAT colour target.
//
// We deliberately store the value in a *colour* image rather than writing
// gl_FragDepth into a depth target: every depth-reading post filter (fog,
// DoF, cavity outline, lens-flare scanner) and the water shader already
// sample depth as `texture(...).r` and linearise it themselves, so feeding
// them an R32F image holding the identical raw value is transparent to their
// maths and avoids depth-attachment / gl_FragDepth portability pitfalls on
// MoltenVK.
//
// Sample 0 (rather than an average) is intentional: depth is non-linear and
// averaging samples across a silhouette would fabricate an in-between surface
// that never existed. Sample 0 is a real, conservative surface depth and is
// what hardware depth resolve modes default to.

#version 450

layout(location = 0) out float fragDepth;

layout(set = 0, binding = 0) uniform sampler2DMS depthMS;

void main() {
    fragDepth = texelFetch(depthMS, ivec2(gl_FragCoord.xy), 0).r;
}
