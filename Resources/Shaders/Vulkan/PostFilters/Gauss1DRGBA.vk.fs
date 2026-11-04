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

// RGBA variant of Gauss1D.vk.fs (which is single-channel, used by DoF CoC).
// Separable 4-tap gaussian, kernel from the original 1dgaussGen.rb; used by
// the bloom (LensDust) downsample chain for GL parity.
// unitShift = (1/w, 0) for horizontal, (0, 1/h) for vertical.
// Pair with PassThrough.vk.vs.

#version 450

layout(binding = 0) uniform sampler2D mainTexture;

layout(push_constant) uniform Params {
    vec2 unitShift;
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 s1 = pc.unitShift * 2.30654399138844;
    vec2 s2 = pc.unitShift * 0.629455560633963;
    const float w1 = 0.178704407070903;
    const float w2 = 0.321295592929097;

    outColor  = texture(mainTexture, texCoord - s1) * w1;
    outColor += texture(mainTexture, texCoord - s2) * w2;
    outColor += texture(mainTexture, texCoord + s2) * w2;
    outColor += texture(mainTexture, texCoord + s1) * w1;
}
