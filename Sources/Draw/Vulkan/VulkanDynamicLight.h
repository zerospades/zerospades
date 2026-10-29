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

#pragma once

#include <Client/IRenderer.h>
#include <Core/Math.h>
#include <array>

namespace spades {
	namespace draw {
		/** A dynamic light of the frame, with what its passes need of it worked out
		 * once, when it is added to the scene. */
		class VulkanDynamicLight {
			client::DynamicLightParam param;
			Matrix4 projMatrix;

			/** World-space clip planes, oriented inside (spotlight only) */
			std::array<Plane3, 4> clipPlanes;

			/** `(point2 - origin).GetSquaredLength()` (linear light only) */
			float poweredLength = 0.0F;

		public:
			/**
			 * How far past the edge of its image a spotlight still lights, as a
			 * fraction of the image's half width: its cone fades out between `0.8`
			 * and this, as `GLDynamicLight::SpotFadeEnd` has it.
			 */
			static constexpr float SpotFadeEnd = 1.1F;

			VulkanDynamicLight(const client::DynamicLightParam& param);
			const client::DynamicLightParam& GetParam() const { return param; }

			/** Projects a world-space point onto a spotlight's image, `[0, 1]` across it
			 * after the divide. */
			const Matrix4& GetProjectionMatrix() const { return projMatrix; }

			bool Cull(const AABB3&) const;
			bool SphereCull(const Vector3& center, float radius) const;

			/** The tangent of a spotlight's half angle: how far its image reaches
			 * sideways per block ahead. */
			float GetSpotTangent() const;

			/** A sphere holding everything the light reaches. */
			void GetBoundingSphere(Vector3& center, float& radius) const;
		};
	}
}
