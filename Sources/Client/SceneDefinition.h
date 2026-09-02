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
			Vector3 viewOrigin;
			Vector3 viewAxis[3];
			float zNear, zFar;
			bool skipWorld;

			float depthOfFieldFocalLength;
			float depthOfFieldNearBlurStrength;
			float depthOfFieldFarBlurStrength;

			unsigned int time;

			bool denyCameraBlur;

			float blurVignette;
			float globalBlur;
			float saturation;
			float radialBlur;

			SceneDefinition() {
				viewportLeft = viewportTop = 0;
				viewportWidth = viewportHeight = 0;
				fovX = fovY = 0.0F;
				viewOrigin = MakeVector3(0, 0, 0);
				viewAxis[0] = MakeVector3(1, 0, 0);
				viewAxis[1] = MakeVector3(0, 1, 0);
				viewAxis[2] = MakeVector3(0, 0, 1);
				zNear = zFar = 0.0F;
				skipWorld = false;
				depthOfFieldFocalLength = 0.0F;
				depthOfFieldNearBlurStrength = 1.0F;
				depthOfFieldFarBlurStrength = 0.0F;
				denyCameraBlur = true;
				time = 0;
				blurVignette = 0.0F;
				globalBlur = 0.0F;
				saturation = 1.0F;
				radialBlur = 0.0F;
			}

			Matrix4 ToOpenGLProjectionMatrix() const;
			Matrix4 ToVulkanProjectionMatrix() const;
			Matrix4 ToViewMatrix() const;
		};
	} // namespace client
} // namespace spades