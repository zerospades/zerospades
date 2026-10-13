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

// The scene's screen-space ambient occlusion where a fragment of a lit pass lands,
// for the fragment stages of the passes binding `SceneView.glsl`'s set.

#ifndef SCENE_AMBIENT_OCCLUSION_GLSL
#define SCENE_AMBIENT_OCCLUSION_GLSL

#include "SceneView.glsl"

/** How much of the ambient light reaches this fragment past what is around it,
 * from 0 to 1: 1 where the view has no ambient occlusion */
float SceneAmbientOcclusion() {
	if (sceneView.options.x == 0.0)
		return 1.0;
	return texelFetch(sceneAmbientOcclusionTexture, ivec2(gl_FragCoord.xy), 0).x;
}

#endif
