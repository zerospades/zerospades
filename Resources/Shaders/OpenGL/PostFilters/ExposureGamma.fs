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

// Applies the auto exposure's gain and the HDR gamma in one pass.

uniform sampler2D mainTexture;
// 1x1: the gain the auto exposure settled on
uniform sampler2D exposureTexture;
uniform float gamma;

varying vec2 texCoord;

void main() {
	vec3 gain = texture2D(exposureTexture, vec2(0.5)).xyz;
	vec3 color = texture2D(mainTexture, texCoord).xyz * gain;
	gl_FragColor = vec4(pow(color, vec3(gamma)), 1.0);
}
