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

// DoF final composite fragment shader.
// Port of OpenGL/PostFilters/DoFMix.fs.
//
// Blends the sharp scene with the two blur accumulations using the CoC as
// the blend factor.  When blurredOnly is non-zero (high-quality path), the
// output is always the blurred result regardless of CoC.
// Linear framebuffer: no gamma encode/decode.
//
// Descriptor set layout (set 0) — binding order mirrors GL uniform names:
//   binding 0 — cocTexture    (sampler2D, R8 CoC)
//   binding 1 — blurTexture2  (sampler2D, colour: second blur accumulation)
//   binding 2 — blurTexture1  (sampler2D, colour: first blur accumulation)
//   binding 3 — mainTexture   (sampler2D, colour: original sharp scene)

#version 450

layout(binding = 0) uniform sampler2D cocTexture;
layout(binding = 1) uniform sampler2D blurTexture2;
layout(binding = 2) uniform sampler2D blurTexture1;
layout(binding = 3) uniform sampler2D mainTexture;

layout(push_constant) uniform Params {
    int blurredOnly; // non-zero for high-quality path
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
    float coc = texture(cocTexture, texCoord).r;

    vec4 a = texture(mainTexture, texCoord);
    vec4 b = texture(blurTexture1, texCoord);
    b += texture(blurTexture2, texCoord) * 2.0;
    b *= (1.0 / 3.0);

    float per = min(1.0, coc * 5.0);
    outColor = (pc.blurredOnly != 0) ? b : mix(a, b, per);
}
