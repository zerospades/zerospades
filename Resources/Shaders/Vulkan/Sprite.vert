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
#extension GL_GOOGLE_include_directive : require

// A sprite drawn in the scene's pass, depth-tested against it: an instance per
// sprite, its quad made here from the vertex index.

#include "SpriteView.glsl"

layout(location = 0) in vec4 centerRadiusAttribute;
layout(location = 1) in vec4 colorAttribute;
layout(location = 2) in float angleAttribute;

layout(location = 0) out vec4 color;
layout(location = 1) out vec2 texCoord;
layout(location = 2) out vec4 fogDensity;

void main() {
	vec2 corner = SpriteCorner(gl_VertexIndex);
	vec3 pos = SpriteCornerPosition(centerRadiusAttribute.xyz, centerRadiusAttribute.w,
	                                angleAttribute, corner);

	gl_Position = spriteView.projectionView * vec4(pos, 1.0);
	color = colorAttribute;
	texCoord = corner * 0.5 + 0.5;
	fogDensity = vec4(SpriteFogDensity(pos));
}
