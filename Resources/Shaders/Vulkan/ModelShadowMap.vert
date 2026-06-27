/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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

// Voxel models need a full affine transform per instance (rotation + scale +
// translation), so unlike the map-chunk shadow shader (ShadowMap.vert, which
// only translates by a vec3) this takes the complete light-space MVP as a push
// constant. mvp = lightProjectionView * modelMatrix * translate(origin),
// computed on the CPU per instance (the origin offset matches the sunlight
// path's `position + modelOrigin`). No descriptor set is needed.
layout(push_constant) uniform PushConstants {
    mat4 mvp;
} pushConstants;

// Model vertex format: uint8 x,y,z at location 0 (see VulkanOptimizedVoxelModel::Vertex)
layout(location = 0) in uvec3 inPosition;

void main() {
    gl_Position = pushConstants.mvp * vec4(vec3(inPosition), 1.0);
}
