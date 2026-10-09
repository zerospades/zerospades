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
	// Where in the scene's depth the first-person view's models end
	float firstPersonDepthEnd;
	// Where the lamp is in the scene's depth, and how far around that its lens spreads
	vec2 sourceCoord;
	vec2 sourceSpread;
	// The distance along the view axis nearer than which something hides the lamp
	float sourceDepth;
	float zNear;
	float zFar;
	float outputIsLinear;
} pc;

layout(set = 0, binding = 1) uniform sampler2D depthTexture;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec2 depthCoord;
layout(location = 2) out float visibility;

// The distance along the view axis a depth of the scene stands for: its projection
// maps that distance to [0, 1].
float LinearDepth(float depth) {
	return pc.zNear * pc.zFar / (pc.zFar - depth * (pc.zFar - pc.zNear));
}

// 1 if nothing in front of the lamp is drawn at `offset` across its lens
float SourceTap(vec2 offset) {
	vec2 coord = pc.sourceCoord + offset * pc.sourceSpread;

	// Off the frame, nothing is known to be in front of it.
	if (any(lessThan(coord, vec2(0.0))) || any(greaterThan(coord, vec2(1.0))))
		return 1.0;

	ivec2 size = textureSize(depthTexture, 0);
	float depth = texelFetch(depthTexture, min(ivec2(coord * vec2(size)), size - 1), 0).r;

	// The first-person view's models are drawn over the glare, and say nothing of
	// what stands before the lamp.
	if (depth < pc.firstPersonDepthEnd)
		return 1.0;

	return LinearDepth(depth) < pc.sourceDepth ? 0.0 : 1.0;
}

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

	// How much of the lamp is in sight, over a 3x3 grid across its lens: anything
	// the scene draws in front of it, a player as much as a wall, hides it, and one
	// half in front of it hides half its glare.
	float seen = 0.0;
	for (int y = -1; y <= 1; y++)
		for (int x = -1; x <= 1; x++)
			seen += SourceTap(vec2(float(x), float(y)));
	visibility = seen * (1.0 / 9.0);
}
