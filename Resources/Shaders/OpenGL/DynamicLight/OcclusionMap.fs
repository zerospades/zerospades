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

// Traces the ray of one texel of a spotlight's tile of
// `GLDynamicLightOcclusionMaps`: how far it gets from the light before it enters a
// solid block, up to the light's reach.

// MapOcclusion.fs
float DynamicLightMapWalk(vec3 from, vec3 to, bool stopAtTarget);

uniform vec3 lightOrigin;
// The light's image's axes and its own
uniform vec3 lightAxisX;
uniform vec3 lightAxisY;
uniform vec3 lightAxisZ;
// How far sideways the tile reaches per block ahead, edge to edge
uniform float lightSpread;
uniform float lightReach;

varying vec2 tileCoord;

void main() {
	vec2 slope = (tileCoord - 0.5) * lightSpread;
	vec3 direction = normalize(lightAxisZ + lightAxisX * slope.x + lightAxisY * slope.y);

	float t = DynamicLightMapWalk(lightOrigin, lightOrigin + direction * lightReach, false);
	gl_FragColor = vec4(min(t, 1.0) * lightReach);
}
