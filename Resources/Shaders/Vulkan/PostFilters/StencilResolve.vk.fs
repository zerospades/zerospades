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

// Stencil resolve for MSAA.
//
// The stencil counterpart of DepthResolve.vk.fs: read sample 0 of the
// multisampled stencil, through the depth-stencil image's stencil-aspect view,
// and write it into a single-sample R8_UINT colour target. Sample 0, as the
// depth's resolve takes, so a resolved texel's bits describe the very surface
// its resolved depth is.

#version 450

layout(location = 0) out uint fragStencil;

layout(set = 0, binding = 0) uniform usampler2DMS stencilMS;

void main() {
    fragStencil = texelFetch(stencilMS, ivec2(gl_FragCoord.xy), 0).r;
}
