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

// Lens flare additive sprite — fragment shader.
//
// Port of Shaders/OpenGL/LensFlare/Draw.fs.  Output is additively
// blended (SRC = ONE, DST = ONE) into the post-process colour buffer.
//
// Multiplies the flare colour (push_constant.color) by:
//   - visibility (the soft sun-disc mask),
//   - flare sprite (per-flare texture),
//   - modulation mask (full-screen mask masking some reflections).

#version 450

layout(binding = 0) uniform sampler2D visibilityTexture;
layout(binding = 1) uniform sampler2D modulationTexture;
layout(binding = 2) uniform sampler2D flareTexture;

layout(push_constant) uniform Params {
    vec4 drawRange;
    vec4 color;
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 1) in  vec2 modulationTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    float vis = texture(visibilityTexture, texCoord).x;
    vec3 rgb = pc.color.rgb * vis;
    rgb *= texture(flareTexture, texCoord).xyz;
    rgb *= texture(modulationTexture, modulationTexCoord).xyz;
    outColor = vec4(rgb, 1.0);
}
