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

// Lens flare additive sprite — vertex shader.
//
// Port of Shaders/OpenGL/LensFlare/Draw.vs.
//
// Renders a clip-space rectangle (push_constant.drawRange) carrying
// a flare/dust/reflection sprite.  The C++ side has already converted
// the sprite's screen-space rectangle from GL NDC (+Y up) to Vulkan
// NDC (+Y down) so the vertex shader just uses drawRange verbatim.
//
// modulationTexCoord is sampled from the masks; it tracks gl_Position
// across the full screen so a single mask spans the whole image, the
// same way the GL version does.

#version 450

layout(push_constant) uniform Params {
    vec4 drawRange; // (minX, minY, maxX, maxY) in clip space
    vec4 color;     // RGB tint (a unused)
} pc;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec2 modulationTexCoord;

void main() {
    vec2 quad[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    vec2 p = quad[gl_VertexIndex];

    gl_Position = vec4(mix(pc.drawRange.xy, pc.drawRange.zw, p), 0.5, 1.0);
    texCoord = p;
    modulationTexCoord = gl_Position.xy * 0.5 + 0.5;
}
