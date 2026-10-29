/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

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

uniform mat4 projectionViewMatrix;
uniform mat4 viewMatrix;
uniform vec3 rightVector;
uniform vec3 upVector;
uniform vec3 frontVector;
uniform vec3 viewOriginVector;

#include "SpriteLight.glsl"

attribute vec4 positionAttribute;
// The corner on the sprite, its rotation, and 1 if it scatters the light it is lit
// by, 0 if it emits its own
attribute vec4 spritePosAttribute;
attribute vec4 colorAttribute;

varying vec4 color;
varying vec2 texCoord;
varying vec4 fogDensity;

vec4 ComputeFogDensity(float poweredLength);

void main() {
	vec3 pos = positionAttribute.xyz;
	float radius = positionAttribute.w;

	vec3 right = rightVector * radius;
	vec3 up = upVector * radius;

	float angle = spritePosAttribute.z;
	float c = cos(angle), s = sin(angle);
	vec2 sprP;
	sprP.x = dot(spritePosAttribute.xy, vec2(c, -s));
	sprP.y = dot(spritePosAttribute.xy, vec2(s, c));
	sprP *= radius;
	pos += right * sprP.x;
	pos += up * sprP.y;

	gl_Position = projectionViewMatrix * vec4(pos, 1.0);

	color = colorAttribute;
	if (spritePosAttribute.w > 0.5)
		color = LightSprite(color, SpriteLight(positionAttribute.xyz, pos, frontVector));

	// sprite texture coord
	texCoord = spritePosAttribute.xy * 0.5 + 0.5;

	// fog.
	// cannot gamma correct because sprite may be
	// alpha-blended.
	vec2 horzRelativePos = pos.xy - viewOriginVector.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	fogDensity = ComputeFogDensity(horzDistance);
}