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

// Vulkan port of PostFilters/CameraBlur.fs.
// Offscreen buffer is linear (LINEAR_FRAMEBUFFER), so no
// linearize / sqrt round trip. Depth-weighted 5-tap smear along the
// per-pixel motion vector; depth^2 weighting keeps the view weapon
// (depth ~[0,0.1]) mostly unsmeared.

layout(binding = 0) uniform sampler2D mainTexture;
layout(binding = 1) uniform sampler2D depthTexture;

layout(push_constant) uniform CameraBlurPC {
	mat4  reverseMatrix;
	float shutterTimeScale;
} pc;

layout(location = 0) in vec2 newCoord;
layout(location = 1) in vec3 oldCoord;

layout(location = 0) out vec4 fragColor;

vec4 getSample(vec2 coord) {
	vec3 color = texture(mainTexture, coord).xyz;
	float depth = texture(depthTexture, coord).x;
	float weight = depth * depth;
	weight = min(weight, 1.0) + 0.0001;
	return vec4(color * weight, weight);
}

void main() {
	vec2 nextCoord = newCoord;
	vec2 prevCoord = oldCoord.xy / oldCoord.z;
	vec2 coord;

	vec4 sum;

	coord = mix(nextCoord, prevCoord, 0.0);
	sum = getSample(coord);

	// use latest sample's weight for camera blur strength
	float allWeight = sum.w;
	vec4 sum2;

	sum /= sum.w;

	coord = mix(nextCoord, prevCoord, pc.shutterTimeScale * 0.2);
	sum2 = getSample(coord);

	coord = mix(nextCoord, prevCoord, pc.shutterTimeScale * 0.4);
	sum2 += getSample(coord);

	coord = mix(nextCoord, prevCoord, pc.shutterTimeScale * 0.6);
	sum2 += getSample(coord);

	coord = mix(nextCoord, prevCoord, pc.shutterTimeScale * 0.8);
	sum2 += getSample(coord);

	sum += sum2 * allWeight;

	fragColor = vec4(sum.xyz / sum.w, 1.0);
}
