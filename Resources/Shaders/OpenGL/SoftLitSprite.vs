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
uniform vec2 zNearFar;

attribute vec4 positionAttribute;
attribute vec3 spritePosAttribute;
attribute vec4 colorAttribute;
attribute vec3 emissionAttribute;
attribute vec4 dlRAttribute;
attribute vec4 dlGAttribute;
attribute vec4 dlBAttribute;

varying vec4 color;
varying vec3 emission;
varying vec4 texCoord;
varying vec4 fogDensity;
varying vec4 depthRange;
varying vec3 normal;
varying vec4 dlR, dlG, dlB;

void PrepareShadow(vec3 worldOrigin, vec3 normal);
vec4 ComputeFogDensity(float poweredLength);

void main() {
	vec3 center = positionAttribute.xyz;

	vec3 pos = center;
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

	// compute normal for virtual volume sprite
	normal = (pos - center) - frontVector;
	normal = normalize(normal);
	PrepareShadow(pos, normal);

	// move sprite to the front of the volume
	float centerDepth = dot(center - viewOriginVector, frontVector);
	depthRange.xy = vec2(centerDepth) + vec2(-1.0, 1.0) * radius;

	// clip the volume by the near clip plane
	float frontDepth = depthRange.x;
	frontDepth = max(frontDepth, 0.3);
	frontDepth = min(frontDepth, depthRange.y);
	depthRange.w = frontDepth;

	// Along the lines of sight, so the sprite still covers the same pixels: the
	// quad faces the camera, so all of it is at `centerDepth`, and scaling it about
	// the eye brings all of it to `frontDepth`. A centre at or behind the eye has
	// no such projection to keep.
	if (centerDepth > 0.001)
		pos = viewOriginVector + (pos - viewOriginVector) * (frontDepth / centerDepth);
	else
		pos += frontVector * (frontDepth - centerDepth);

	gl_Position = projectionViewMatrix * vec4(pos, 1.0);

	color = colorAttribute;
	emission = emissionAttribute;
	dlR = dlRAttribute;
	dlG = dlGAttribute;
	dlB = dlBAttribute;

	// sprite texture coord
	texCoord.xy = spritePosAttribute.xy * 0.5 + 0.5;

	// depth texture coord
	texCoord.zw = vec2(0.5) + (gl_Position.xy / gl_Position.w) * 0.5;

	// fog.
	// FIXME: cannot gamma correct because sprite may be
	// alpha-blended.
	vec2 horzRelativePos = pos.xy - viewOriginVector.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	fogDensity = ComputeFogDensity(horzDistance);

	// precompute some value in vertex shader to
	// reduce instruction count in frag. shader
	depthRange.z = 1.0 / (depthRange.y - depthRange.w);
	depthRange.y = depthRange.x;
	depthRange.x *= -depthRange.z;

	depthRange.y /= (zNearFar.x * zNearFar.y);
	depthRange.z *= (zNearFar.x * zNearFar.y);
}