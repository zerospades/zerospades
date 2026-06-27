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

// Vulkan port of PostFilters/CameraBlur.vs.
// Fullscreen triangle; per-vertex reprojection of the current-frame
// texcoord into last frame's frame via reverseMatrix.

layout(push_constant) uniform CameraBlurPC {
	mat4  reverseMatrix;
	float shutterTimeScale;
} pc;

layout(location = 0) out vec2 newCoord;
layout(location = 1) out vec3 oldCoord;

void main() {
	vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(uv * 2.0 - 1.0, 0.5, 1.0);

	newCoord = uv;

	// GL math runs in bottom-left-origin coords; our texcoords are
	// top-left-origin, so flip Y going in and coming out.
	vec4 cvt = vec4(uv.x - 0.5, 0.5 - uv.y, 0.0, 1.0);
	vec3 o = (pc.reverseMatrix * cvt).xyz;
	oldCoord = vec3(o.x + 0.5 * o.z, -o.y + 0.5 * o.z, o.z);
}
