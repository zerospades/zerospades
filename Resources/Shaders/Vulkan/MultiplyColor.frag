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

layout(push_constant) uniform PushConstants {
	vec3 color;
} pc;

layout(location = 0) out vec4 fragColor;

void main() {
	// Output the multiply color with full alpha.
	// The pipeline blend state (ZERO, SRC_COLOR) performs:
	//   dst = dst * src_color
	// which tints the framebuffer by pc.color.
	fragColor = vec4(pc.color, 1.0);
}
