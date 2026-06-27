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

// Lens flare visibility scanner — vertex shader.
//
// Port of Shaders/OpenGL/LensFlare/Scanner.vs.
//
// Renders a centred 1.0×1.0 NDC quad (clip space [-0.5, 0.5]) into a
// 64×64 visibility buffer.  Each fragment shadow-compares the depth
// texture at scanRange → scanPos.xy with depth scanPos.z, producing
// a soft visibility disc that is later 1D-Gauss blurred three times.
//
// Six vertices generated from gl_VertexIndex (triangle list).

#version 450

layout(push_constant) uniform Params {
    vec4 scanRange; // (uvMinX, uvMinY, uvMaxX, uvMaxY) in depth-texture coords
    float scanZ;    // depth value to compare against (1.0 = far)
} pc;

layout(location = 0) out vec3 scanPos;
layout(location = 1) out vec2 circlePos;

void main() {
    // Triangle list: two CCW triangles forming a [0,1]² quad.
    vec2 quad[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    vec2 p = quad[gl_VertexIndex];

    scanPos.xy = mix(pc.scanRange.xy, pc.scanRange.zw, p);
    scanPos.z = pc.scanZ;

    // Centre the quad in NDC [-0.5, +0.5] so the visibility disc is
    // surrounded by black border pixels for the subsequent blur.
    gl_Position = vec4(p - 0.5, 0.5, 1.0);

    circlePos = p * 2.0 - 1.0; // [-1, +1]
}
