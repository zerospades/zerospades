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

// Fog2 filter vertex shader.
// Port of OpenGL/PostFilters/Fog2.vs.
// Computes the view-centric world position partial (without the depth
// contribution) for each corner; the fragment shader adds the per-pixel
// depth contribution to finish the world-position reconstruction.
// Draw with vkCmdDraw(cmd, 3, 1, 0, 0) — no vertex buffer needed.
//
// The CPU must pass viewProjectionMatrixInv as the inverse of the matrix that
// maps UV texture-space coordinates [0,1]^2 to view-centric clip space.  The
// scene is rendered with a negative-height viewport (Vulkan Y-flip), so the
// stored texture maps uv.y=0 to clip_y=+1.  The CPU forms:
//   viewProjectionMatrixInv = inverse( Scale(0.5, -0.5, 1) * Translate(1, -1, 0)
//                                      * (proj_vk * view_no_t) )

#version 450

layout(push_constant) uniform Params {
    mat4 viewProjectionMatrixInv; // [0..63]   UV → view-centric world (VS+FS)
    vec4 viewOriginFogDist;       // [64..79]  xyz=viewOrigin, w=fogDistance (FS)
    vec4 sunlightScale;           // [80..95]  xyz (FS)
    vec4 ambientScale;            // [96..111] xyz (FS)
    vec4 radiosityScale;          // [112..127] xyz (FS)
    vec4 ditherFrame;             // [128..143] xy=per-frame noise seed (FS)
} pc;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec4 viewcentricWorldPositionPartial;

void main() {
    vec2 uv     = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    texCoord    = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);

    viewcentricWorldPositionPartial = pc.viewProjectionMatrixInv * vec4(uv, 0.0, 1.0);
}
