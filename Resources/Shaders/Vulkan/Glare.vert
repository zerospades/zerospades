/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#version 450

// A glare (`client::GlareParam`): a quad centred where its light projects, drawn
// onto the swapchain image over the finished frame, before the 2D drawing.

layout(push_constant) uniform PushConstants {
	// The quad, in GL's normalized device coordinates (y up): min x, min y, max x, max y
	vec4 drawRange;
	vec3 color;
	float firstPersonDepthEnd;
	float outputIsLinear;
} pc;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec2 depthCoord;

void main() {
	// A triangle strip: (0, 0), (1, 0), (0, 1), (1, 1)
	vec2 corner = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1);
	vec2 ndc = mix(pc.drawRange.xy, pc.drawRange.zw, corner);

	// The swapchain image runs from the top down
	gl_Position = vec4(ndc.x, -ndc.y, 0.5, 1.0);

	// The image's first row at the top, as the 2D drawing puts it
	texCoord = vec2(corner.x, 1.0 - corner.y);

	// The scene is drawn through a flipped viewport, so its images run from the
	// top down, as the soft sprites sample them too
	depthCoord = vec2(0.5 + ndc.x * 0.5, 0.5 - ndc.y * 0.5);
}
