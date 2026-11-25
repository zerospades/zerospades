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

// DoF CoC mix fragment shader.
// Port of OpenGL/PostFilters/DoFCoCMix.fs.
//
// Blends the raw CoC with its blurred version to spread CoC into
// neighbouring pixels (avoids hard edges in the blur radius field).
//
// Descriptor set layout (set 0):
//   binding 0 — cocTexture     (sampler2D, R8 raw CoC)
//   binding 1 — cocBlurTexture (sampler2D, R8 blurred CoC)

#version 450

layout(binding = 0) uniform sampler2D cocTexture;
layout(binding = 1) uniform sampler2D cocBlurTexture;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out float outCoC;

void main() {
    float coc     = texture(cocTexture,     texCoord).r;
    float cocBlur = texture(cocBlurTexture, texCoord).r;

    float op = 2.0 * max(cocBlur, coc) - coc;
    op = max(op, coc);
    outCoC = op;
}
