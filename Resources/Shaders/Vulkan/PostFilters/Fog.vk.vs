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

// Fog filter (Fog1 style) vertex shader.
// Port of OpenGL/PostFilters/Fog.vs.
// Computes per-vertex shadow-space origin and ray direction; the fragment
// shader uses them for voxel shadow-map traversal.
// Draw with vkCmdDraw(cmd, 3, 1, 0, 0) — no vertex buffer needed.

#version 450

layout(push_constant) uniform Params {
    vec4 viewOriginPad;  // xyz = viewOrigin
    vec4 viewAxisUp;     // xyz
    vec4 viewAxisSide;   // xyz
    vec4 viewAxisFront;  // xyz
    vec4 fovZNearFar;    // xy = fov (tan half-angle), z = zNear, w = zFar
    vec4 fogColorDist;   // xyz = fogColor (linear), w = fogDistance
} pc;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec3 viewTan;
layout(location = 2) out vec3 viewDir;
layout(location = 3) out vec3 shadowOrigin;
layout(location = 4) out vec3 shadowRayDirection;

vec3 transformToShadow(vec3 v) {
    v.y -= v.z;
    v *= vec3(1.0, 1.0, 1.0 / 255.0);
    return v;
}

void main() {
    vec2 uv     = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 scrPos = uv * 2.0 - 1.0;
    gl_Position = vec4(scrPos, 0.5, 1.0);

    texCoord = uv;

    vec2 fov = pc.fovZNearFar.xy;
    viewTan.xy = mix(-fov, fov, uv);
    viewTan.z  = 1.0;

    vec3 viewOrigin = pc.viewOriginPad.xyz;
    shadowOrigin = transformToShadow(viewOrigin);

    viewDir  = pc.viewAxisUp.xyz    * viewTan.y;
    viewDir += pc.viewAxisSide.xyz  * viewTan.x;
    viewDir += pc.viewAxisFront.xyz;

    shadowRayDirection = transformToShadow(viewDir);
}
