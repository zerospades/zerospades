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

// Fullscreen triangle vertex shader shared by all post-process filters.
// Vertices are generated from gl_VertexIndex; no vertex buffer is needed.
// Draw with vkCmdDraw(cmd, 3, 1, 0, 0).

layout(location = 0) out vec2 texCoord;

void main() {
	vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	texCoord    = uv;
	gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
