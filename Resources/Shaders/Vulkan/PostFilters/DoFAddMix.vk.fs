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

// DoF add-mix fragment shader.
// Mirrors the GammaMix (LINEAR_FRAMEBUFFER path) call in GLDepthOfFieldFilter::AddMix():
//   outColor = texture1 * 0.5 + texture2 * 0.5
//
// Descriptor set layout (set 0):
//   binding 0 — texture1 (sampler2D, colour image)
//   binding 1 — texture2 (sampler2D, colour image)

#version 450

layout(binding = 0) uniform sampler2D texture1;
layout(binding = 1) uniform sampler2D texture2;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 c1 = texture(texture1, texCoord);
    vec4 c2 = texture(texture2, texCoord);
    outColor = vec4(c1.rgb * 0.5 + c2.rgb * 0.5, 1.0);
}
