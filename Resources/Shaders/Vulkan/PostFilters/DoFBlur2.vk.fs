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

// DoF directional scatter blur — high quality (CoC-weighted).
// Port of OpenGL/PostFilters/DoFBlur2.fs.
//
// Eight taps along `offset * coc`, weighted by per-sample CoC so that
// out-of-focus pixels dominate in-focus ones (avoids leaking).  Used for
// the high-quality DoF path (r_depthOfField >= 2).
// Linear framebuffer: no gamma encode/decode.
//
// Descriptor set layout (set 0):
//   binding 0 — mainTexture (sampler2D, colour image)
//   binding 1 — cocTexture  (sampler2D, R8 CoC image at full resolution)

#version 450

layout(binding = 0) uniform sampler2D mainTexture;
layout(binding = 1) uniform sampler2D cocTexture;

layout(push_constant) uniform Params {
    vec2 offset; // max CoC displacement vector in texture-coordinate space
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

vec4 sampleDoF(vec2 at) {
    vec4 color = texture(mainTexture, at);
    color.a    = texture(cocTexture,  at).r + 0.001;
    color.rgb *= color.a;
    return color;
}

void main() {
    float coc = texture(cocTexture, texCoord).r;

    vec4 offsets  = vec4(0.0, 0.25, 0.5, 0.75) * coc;
    vec4 offsets2 = offsets + coc * 0.125;

    vec4 v = vec4(0.0);
    v += sampleDoF(texCoord);
    v += sampleDoF(texCoord + pc.offset * offsets.y);
    v += sampleDoF(texCoord + pc.offset * offsets.z);
    v += sampleDoF(texCoord + pc.offset * offsets.w);
    v += sampleDoF(texCoord + pc.offset * offsets2.x);
    v += sampleDoF(texCoord + pc.offset * offsets2.y);
    v += sampleDoF(texCoord + pc.offset * offsets2.z);
    v += sampleDoF(texCoord + pc.offset * offsets2.w);
    v.rgb *= 1.0 / v.a;

    outColor = vec4(v.rgb, 1.0);
}
