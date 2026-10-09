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

// How the scene's passes see the world: `VulkanSceneView`, the same for every draw
// of a pass. The main view, or the mirrored one in the water's reflection.

#ifndef SCENE_VIEW_GLSL
#define SCENE_VIEW_GLSL

layout(set = 3, binding = 0, std140) uniform SceneView {
	// World to clip space
	mat4 projectionView;
	// World to view space
	mat4 view;
	// xyz: the eye; w: the fog distance
	vec4 eyeFogDistance;
	// xyz: the colour the solid passes fade to, linear; w: the height below which
	// the mirror clips models away, or a height no model reaches
	vec4 fogColorMirrorClipZ;
	// xyz: towards the sun
	vec4 sunDirection;
} sceneView;

#endif
