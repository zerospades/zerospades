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

// One tile of `GLDynamicLightOcclusionMaps`, filling the viewport

attribute vec2 positionAttribute;

// Where the texel lies on the tile, from 0 to 1
varying vec2 tileCoord;

void main() {
	gl_Position = vec4(positionAttribute * 2.0 - 1.0, 0.5, 1.0);
	tileCoord = positionAttribute;
}
