#extension GL_EXT_texture_array : require
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
uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform vec3 viewOriginVector;

// [x, y]
attribute vec2 positionAttribute;

varying vec3 fogDensity;
varying vec3 screenPosition;
varying vec3 viewPosition;
varying vec3 worldPosition;
varying vec2 worldPositionOriginal;
// The height of the water's surface, before the waves move it up or down
varying float surfaceHeight;

uniform sampler2DArray waveTextureArray;

void PrepareShadow(vec3 worldOrigin, vec3 normal);
vec4 ComputeFogDensity(float poweredLength);

vec3 DisplaceWater(vec2 worldPos) {
	vec4 waveCoord = worldPos.xyxy * vec4(vec2(0.08), vec2(0.15704))
		+ vec4(0.0, 0.0, 0.754, 0.1315);

	vec2 waveCoord2 = worldPos.xy * 0.02344 + vec2(0.154, 0.7315);

	float wave = texture2DArrayLod(waveTextureArray, vec3(waveCoord.xy, 0.0), 0.0).w;
	float disp = mix(-0.1, 0.1, wave) * 0.4;

	float wave2 = texture2DArrayLod(waveTextureArray, vec3(waveCoord.zw, 1.0), 0.0).w;
	disp += mix(-0.1, 0.1, wave2) * 0.2;

	float wave3 = texture2DArrayLod(waveTextureArray, vec3(waveCoord2.xy, 2.0), 0.0).w;
	disp += mix(-0.1, 0.1, wave3) * 2.5;

	float waveSmoothed1 = texture2DArrayLod(waveTextureArray, vec3(waveCoord2.xy, 2.0), 4.0).w;
	float waveSmoothed2 = texture2DArrayLod(waveTextureArray, vec3(waveCoord2.xy + vec2(1.0 / 16.0, 0.0), 2.0), 3.0).w;
	float waveSmoothed3 = texture2DArrayLod(waveTextureArray, vec3(waveCoord2.xy + vec2(0.0, 1.0 / 16.0), 2.0), 3.0).w;
	vec2 dispHorz = vec2(waveSmoothed2 - waveSmoothed1, waveSmoothed3 - waveSmoothed1) * -16.0;

	return vec3(dispHorz, disp * 4.0);
}

void main() {
	vec4 vertexPos = vec4(positionAttribute.xy, 0.0, 1.0);

	worldPosition = (modelMatrix * vertexPos).xyz;
	worldPositionOriginal = worldPosition.xy;
	surfaceHeight = worldPosition.z;
	worldPosition += DisplaceWater(worldPositionOriginal);
	viewPosition = (viewMatrix * vec4(worldPosition, 1.0)).xyz;
	
	gl_Position = projectionViewMatrix * vec4(worldPosition, 1.0);
	screenPosition = gl_Position.xyw;
	screenPosition.xy = (screenPosition.xy + screenPosition.z) * 0.5;

	vec2 horzRelativePos = worldPosition.xy - viewOriginVector.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	fogDensity = ComputeFogDensity(horzDistance).xyz;

	PrepareShadow(worldPosition, vec3(0.0, 0.0, -1.0));
}