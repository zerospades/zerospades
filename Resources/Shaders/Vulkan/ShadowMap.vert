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

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 projectionViewMatrix;
} ubo;

layout(push_constant) uniform PushConstants {
    vec3 modelOrigin;
} pushConstants;

// Map chunk vertex format: uint8 x,y,z at location 0
layout(location = 0) in uvec3 inPosition;

void main() {
    // Convert uint8 position to world space
    vec3 localPos = vec3(inPosition);
    vec3 worldPos = localPos + pushConstants.modelOrigin;
    gl_Position = ubo.projectionViewMatrix * vec4(worldPos, 1.0);
}
