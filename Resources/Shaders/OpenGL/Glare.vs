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

// The scene's depth, as its depth buffer holds it
uniform sampler2D depthTexture;
uniform vec2 zNearFar;

// Where the light is in `depthTexture`'s coordinates, how far around that its
// projected size reaches, and the nearest depth along the view axis at which
// something in front of it hides it
uniform vec2 sourceCoord;
uniform vec2 sourceSpread;
uniform float sourceDepth;

varying vec2 texCoord;
varying float visibility;

// The distance along the view axis a depth buffer value stands for
float LinearDepth(float depth) {
	float near = zNearFar.x, far = zNearFar.y;
	return 2.0 * near * far / (far + near - (depth * 2.0 - 1.0) * (far - near));
}

// 1 if nothing in front of the light is seen at `offset` within its size
float SourceTap(vec2 offset) {
	vec2 coord = sourceCoord + offset * sourceSpread;

	// Off the frame, nothing is known to be in front of it.
	if (any(lessThan(coord, vec2(0.0))) || any(greaterThan(coord, vec2(1.0))))
		return 1.0;

	return LinearDepth(texture2D(depthTexture, coord).x) < sourceDepth ? 0.0 : 1.0;
}

void main() {
	gl_Position = vec4(mix(drawRange.xy, drawRange.zw, positionAttribute), 0.5, 1.0);
	// The image's first row at the top, as the 2D drawing puts it
	texCoord = vec2(positionAttribute.x, 1.0 - positionAttribute.y);

	// How much of the light is in sight, over a 3x3 grid across it: a light
	// half behind the edge of a wall or a hand shows half its glare.
	float seen = 0.0;
	for (int y = -1; y <= 1; y++)
		for (int x = -1; x <= 1; x++)
			seen += SourceTap(vec2(float(x), float(y)));
	visibility = seen * (1.0 / 9.0);
}
