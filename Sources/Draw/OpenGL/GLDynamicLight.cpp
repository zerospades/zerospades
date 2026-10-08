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

#include <algorithm>
#include <cmath>

#include "GLDynamicLight.h"

namespace spades {
	namespace draw {
		namespace {
			/**
			 * How far past the edge of its image a spotlight still lights, as a
			 * fraction of the image's half width: its cone fades out between `0.8`
			 * and this (`smoothstep(1.1, 0.8, ...)` in `DynamicLight/Lights.fs`).
			 */
			constexpr float kSpotFadeEnd = 1.1F;

			/** The squared distance from `point` to the nearest point of `box`. */
			float SquaredDistance(const AABB3& box, const Vector3& point) {
				const float dx = std::max(std::max(box.min.x - point.x, point.x - box.max.x), 0.0F);
				const float dy = std::max(std::max(box.min.y - point.y, point.y - box.max.y), 0.0F);
				const float dz = std::max(std::max(box.min.z - point.z, point.z - box.max.z), 0.0F);
				return dx * dx + dy * dy + dz * dz;
			}
		} // namespace

		GLDynamicLight::GLDynamicLight(const client::DynamicLightParam& param) : param(param) {
			if (param.type == client::DynamicLightTypeSpotlight) {
				float t = tanf(param.spotAngle * 0.5F);
				Matrix4 mat;
				mat = Matrix4::FromAxis(param.spotAxis[0], param.spotAxis[1], param.spotAxis[2],
				                        param.origin);
				mat = mat * Matrix4::Scale(t * 2.0F, t * 2.0F, 1.0F);

				projMatrix = mat.InversedFast();

				Matrix4 m = Matrix4::Identity();
				m.m[15] = 0.0F;
				m.m[11] = 1.0F;

				m.m[8] += 0.5F;
				m.m[9] += 0.5F;
				projMatrix = m * projMatrix;

				// Construct clipping planes which are oriented inside, around all the cone
				// lights, its fade past the image's edge included.
				t *= kSpotFadeEnd;

				// To do that, first we calculate tangent vectors:
				Vector3 planeTan[] = {
				  param.spotAxis[2] + param.spotAxis[0] * t,
				  param.spotAxis[2] + param.spotAxis[1] * t,
				  param.spotAxis[2] - param.spotAxis[0] * t,
				  param.spotAxis[2] - param.spotAxis[1] * t,
				};

				// Then, use them to derive normal vectors:
				Vector3 planeN[] = {
				  Vector3::Cross(param.spotAxis[1], planeTan[0]),
				  Vector3::Cross(planeTan[1], param.spotAxis[0]),
				  Vector3::Cross(planeTan[2], param.spotAxis[1]),
				  Vector3::Cross(param.spotAxis[0], planeTan[3]),
				};

				// Finally, find planes with these normal vectors:
				for (std::size_t i = 0; i < 4; ++i)
					clipPlanes[i] = Plane3::PlaneWithPointOnPlane(param.origin, planeN[i]);
			}

			if (param.type == client::DynamicLightTypeLinear)
				poweredLength = (param.point2 - param.origin).GetSquaredLength();
		}

		bool GLDynamicLight::Cull(const spades::AABB3& box) const {
			const client::DynamicLightParam& param = GetParam();

			if (param.type == client::DynamicLightTypeSpotlight) {
				for (const auto& plane : clipPlanes) {
					if (!PlaneCullTest(plane, box))
						return false;
				}
			}

			if (param.type == client::DynamicLightTypeLinear) {
				AABB3 inflatedBox = box.Inflate(param.radius);
				Vector3 intersection;
				// TODO: using `OBB3` here is overkill, but `AABB3` doesn't have `RayCast`
				if (!OBB3(inflatedBox).RayCast(param.origin, param.point2 - param.origin, &intersection))
					return false;
				return (intersection - param.origin).GetSquaredLength() <= poweredLength;
			}

			return SquaredDistance(box, param.origin) < param.radius * param.radius;
		}

		void GLDynamicLight::GetBoundingSphere(Vector3& center, float& radius) const {
			const float reach = param.radius;

			switch (param.type) {
				case client::DynamicLightTypeSpotlight: {
					// The spherical sector the cone lights out to its reach.
					const float tanHalf = std::tan(param.spotAngle * 0.5F) * kSpotFadeEnd;
					const float cosHalf = 1.0F / std::sqrt(1.0F + tanHalf * tanHalf);
					const float sinHalf = tanHalf * cosHalf;
					const Vector3 axis = param.spotAxis[2].Normalize();

					if (cosHalf < std::sqrt(0.5F)) {
						// Wide: the sphere through the rim of the cone's base holds the
						// apex and the far end of the reach too.
						center = param.origin + axis * (reach * cosHalf);
						radius = reach * sinHalf;
					} else {
						// Narrow: the sphere through the apex and the rim.
						radius = reach / (2.0F * cosHalf);
						center = param.origin + axis * radius;
					}
				} break;
				case client::DynamicLightTypeLinear: {
					const Vector3 half = (param.point2 - param.origin) * 0.5F;
					center = param.origin + half;
					radius = reach + half.GetLength();
				} break;
				default:
					center = param.origin;
					radius = reach;
					break;
			}
		}

		bool GLDynamicLight::SphereCull(const spades::Vector3& center, float radius) const {
			const client::DynamicLightParam& param = GetParam();

			if (param.type == client::DynamicLightTypeSpotlight) {
				for (const auto& plane : clipPlanes) {
					if (plane.GetDistanceTo(center) < -radius)
						return false;
				}
			} else if (param.type == client::DynamicLightTypeLinear) {
				return Line3::MakeLineSegment(param.origin, param.point2).GetDistanceTo(center) <
				       radius + param.radius;
			}

			float maxDistance = radius + param.radius;
			return (center - param.origin).GetSquaredLength() < maxDistance * maxDistance;
		}
	} // namespace draw
} // namespace spades