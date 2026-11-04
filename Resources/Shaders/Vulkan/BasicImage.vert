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

#version 450

// Must match BasicImage.frag exactly: one range covers both stages.
layout(push_constant) uniform PushConstants {
	vec2 invScreenSizeFactored;
	vec2 invTextureSize;
	vec2 clipCircleCenter; // screen pixels
	float clipCircleRadius; // <= 0 disables the circular clip
	float _pad;
} pushConstants;

layout(location = 0) in vec2 positionAttribute;
layout(location = 1) in vec2 textureCoordAttribute;
layout(location = 2) in vec4 colorAttribute;

layout(location = 0) out vec4 color;
layout(location = 1) out vec2 texCoord;

void main() {
	vec2 pos = positionAttribute;
	// With negative viewport height, Y goes from top (0) to bottom (height)
	// Transform to NDC: X: [0,width] -> [-1,1], Y: [0,height] -> [1,-1]
	pos = pos * pushConstants.invScreenSizeFactored + vec2(-1.0, 1.0);

	gl_Position = vec4(pos, 0.5, 1.0);

	color = colorAttribute;
	texCoord = textureCoordAttribute * pushConstants.invTextureSize;
}
