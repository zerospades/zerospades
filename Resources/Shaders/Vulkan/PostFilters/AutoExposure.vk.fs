/*
 Copyright (c) 2015 Fran6nd

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

// Reads the 1×1 scene-brightness image and outputs the corresponding
// exposure gain.  Rendered into the persistent 1×1 exposure accumulator
// with SRC_ALPHA / ONE_MINUS_SRC_ALPHA blending so that alpha carries
// the temporal adaptation rate.

#version 450

layout(binding = 0) uniform sampler2D mainTexture;

layout(push_constant) uniform Params {
	float minGain;
	float maxGain;
	float rate;
} params;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
	float brightness = texture(mainTexture, vec2(0.5, 0.5)).r;

	// Reverse the 4th-power applied during preprocessing.
	brightness = sqrt(brightness);
	brightness = sqrt(brightness);

	float gain = clamp(0.6 / brightness, params.minGain, params.maxGain);

	// Alpha carries the temporal blend weight; blend mode handles accumulation.
	outColor = vec4(gain, gain, gain, params.rate);
}
