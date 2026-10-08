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

attribute vec2 positionAttribute;

// The quad, in normalized device coordinates: min x, min y, max x, max y
uniform vec4 drawRange;

varying vec2 texCoord;
varying vec2 depthCoord;

void main() {
	gl_Position = vec4(mix(drawRange.xy, drawRange.zw, positionAttribute), 0.5, 1.0);

	// The image's first row at the top, as the 2D drawing puts it
	texCoord = vec2(positionAttribute.x, 1.0 - positionAttribute.y);

	// Where this is in the scene's depth texture
	depthCoord = gl_Position.xy * 0.5 + 0.5;
}
