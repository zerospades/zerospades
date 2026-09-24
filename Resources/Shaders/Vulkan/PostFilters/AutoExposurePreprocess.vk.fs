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

// First-level downsample pass: converts linear-RGB scene colour to a
// brightness estimate suitable for progressive halving to a 1×1 image.

#version 450

layout(binding = 0) uniform sampler2D mainTexture;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
	vec3 color = texture(mainTexture, texCoord).rgb;

	// Desaturate to peak brightness.
	float brightness = max(max(color.r, color.g), color.b);

	// Remove NaN and out-of-range values.
	if (!(brightness >= 0.0 && brightness <= 16.0))
		brightness = 0.05;

	// Clamp to a safe working range.
	brightness = clamp(brightness, 0.05, 1.3);

	// Raise to 4th power to suppress overbright highlights in the average.
	brightness *= brightness;
	brightness *= brightness;

	outColor = vec4(brightness, brightness, brightness, 1.0);
}
