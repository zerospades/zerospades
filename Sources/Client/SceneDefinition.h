/*
 Copyright (c) 2021 yvt

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

#pragma once

#include <Core/Math.h>

namespace spades {
	namespace client {
		struct SceneDefinition {
			int viewportLeft, viewportTop;
			int viewportWidth, viewportHeight;
			float fovX, fovY;
			// A parallel projection instead, taking in `orthoHeight` world
			// units from the top of the viewport to the bottom at every depth;
			// the field of view still gives the viewport's shape, so the width
			// taken in is that height times its ratio. The editors offer it;
			// the game is always a perspective view.
			bool orthogonal;
			float orthoHeight;
			Vector3 viewOrigin;
			Vector3 viewAxis[3];
			float zNear, zFar;
			bool skipWorld;
			/** Draw the world's terrain but not its water surface, nor the mirrored
			 *  scene that feeds it (the model editor wants voxels under the model,
			 *  not the map's sea). */
			bool skipWater;

			/** Whether the frame may draw models flagged `xray` through the world. The
			 * renderer skips the whole pass when this is clear, so it has to be set on
			 * any frame where a model asks to be revealed. */
			bool allowPlayerXRay;

			/** Light models from every direction at once instead of from the sun.
			 *  A voxel has only six face normals, so a directional term splits one
			 *  flat colour into a handful of visibly different shades; the model
			 *  editor wants the voxel's own colour on screen. Ambient occlusion
			 *  still applies, so corners keep a soft shape cue. */
			bool flatModelLighting;
			/** Draw the outline pass whatever `r_outlines` says. Flat lighting drops
			 *  the shading that conveys depth, and a silhouette gives it back. */
			bool forceOutlines;

			float depthOfFieldFocalLength;
			float depthOfFieldNearBlurStrength;
			float depthOfFieldFarBlurStrength;

			unsigned int time;

			bool denyCameraBlur;

			float blurVignette;
			float globalBlur;
			float saturation;
			float radialBlur;

			/** The factor the sun's light is drawn with, in `[0, 1]`. At `0` the sun
			 * casts no light and no shadow. */
			float sunlight;

			/**
			 * The factor the rest of the world's lighting, the fog and the sky are drawn
			 * with, in `[0, 1]`. Dynamic lights are not affected. `1` is full daylight.
			 */
			float daylight;

			SceneDefinition() {
				viewportLeft = viewportTop = 0;
				viewportWidth = viewportHeight = 0;
				fovX = fovY = 0.0F;
				orthogonal = false;
				orthoHeight = 0.0F;
				viewOrigin = MakeVector3(0, 0, 0);
				viewAxis[0] = MakeVector3(1, 0, 0);
				viewAxis[1] = MakeVector3(0, 1, 0);
				viewAxis[2] = MakeVector3(0, 0, 1);
				zNear = zFar = 0.0F;
				skipWorld = false;
				allowPlayerXRay = false;
				skipWater = false;
				flatModelLighting = false;
				forceOutlines = false;
				depthOfFieldFocalLength = 0.0F;
				depthOfFieldNearBlurStrength = 1.0F;
				depthOfFieldFarBlurStrength = 0.0F;
				denyCameraBlur = true;
				time = 0;
				blurVignette = 0.0F;
				globalBlur = 0.0F;
				saturation = 1.0F;
				radialBlur = 0.0F;
				sunlight = 1.0F;
				daylight = 1.0F;
			}

			Matrix4 ToOpenGLProjectionMatrix() const;
			Matrix4 ToVulkanProjectionMatrix() const;
			Matrix4 ToViewMatrix() const;
		};
	} // namespace client
} // namespace spades