/*
 Copyright (c) 2013 yvt

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

#include <Client/IRenderer.h>
#include <Core/Math.h>
#include <array>
#include <cstddef>

namespace spades {
	namespace draw {
		class GLDynamicLight {
			client::DynamicLightParam param;
			Matrix4 projMatrix;

			/** Its row in the renderer's `GLDynamicLightTable` this frame. */
			std::size_t tableRow = 0;

			/** World-space clip planes (spotlight only) */
			std::array<Plane3, 4> clipPlanes;

			/** `(point2 - origin).GetSquaredLength()` (linear light only) */
			float poweredLength;

		public:
			/**
			 * How far past the edge of its image a spotlight still lights, as a
			 * fraction of the image's half width: its cone fades out between `0.8`
			 * and this (`DYNAMIC_LIGHT_SPOT_FADE_END` in `DynamicLight/Lights.fs`).
			 */
			static constexpr float SpotFadeEnd = 1.1F;

			GLDynamicLight(const client::DynamicLightParam &param);
			const client::DynamicLightParam &GetParam() const { return param; }

			const Matrix4 &GetProjectionMatrix() const { return projMatrix; }

			std::size_t GetTableRow() const { return tableRow; }
			void SetTableRow(std::size_t row) { tableRow = row; }

			bool Cull(const AABB3 &) const;

			bool SphereCull(const Vector3 &center, float radius) const;

			/** The tangent of a spotlight's half angle: how far its image reaches
			 * sideways per block ahead. */
			float GetSpotTangent() const;

			/** A sphere holding everything the light reaches. */
			void GetBoundingSphere(Vector3 &center, float &radius) const;
		};
	} // namespace draw
} // namespace spades
