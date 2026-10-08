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

uniform sampler2D glareTexture;
uniform vec3 glareColor;

varying vec2 texCoord;
varying float visibility;

void main() {
	// Alpha premultiplied as the 2D drawing does it, and added onto the frame
	vec4 image = texture2D(glareTexture, texCoord);
	gl_FragColor = vec4(glareColor * image.xyz * (image.w * visibility), 0.0);
}
