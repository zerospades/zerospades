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
	vec3 fogColor;
	float _pad0;
	vec3 viewAxisFront;
	float _pad1;
	vec3 viewAxisUp;
	float _pad2;
	vec3 viewAxisSide;
	float _pad3;
	float fovX;
	float fovY;
} pushConstants;

layout(location = 0) in vec3 viewDir;

layout(location = 0) out vec4 fragColor;

void main() {
	// Match GL: the sky has no procedural shading on the scene pass. It is
	// drawn flat in the linearised fog color and the Fog2 post-pass then
	// adds the directional sunlight, ambient (per-block AO), and radiosity
	// in-scattering — that is what gives the GL sky its pastel gradient.
	fragColor = vec4(pushConstants.fogColor, 1.0);
}
