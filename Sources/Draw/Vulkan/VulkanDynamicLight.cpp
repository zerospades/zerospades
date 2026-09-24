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

#include "VulkanDynamicLight.h"
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		VulkanDynamicLight::VulkanDynamicLight(const client::DynamicLightParam& param) : param(param) {
			SPADES_MARK_FUNCTION();

			if (param.type == client::DynamicLightTypeSpotlight) {
				float t = tanf(param.spotAngle * 0.5f);
				Matrix4 mat;
				mat = Matrix4::FromAxis(param.spotAxis[0], param.spotAxis[1], param.spotAxis[2],
				                        param.origin);
				mat = mat * Matrix4::Scale(t * 2.0f, t * 2.0f, 1.0f);

				projMatrix = mat.InversedFast();

				Matrix4 m = Matrix4::Identity();
				m.m[15] = 0.0f;
				m.m[11] = 1.0f;

				m.m[8] += 0.5f;
				m.m[9] += 0.5f;
				projMatrix = m * projMatrix;

				Vector3 planeTan[] = {
				  param.spotAxis[2] + param.spotAxis[0] * t,
				  param.spotAxis[2] + param.spotAxis[1] * t,
				  param.spotAxis[2] - param.spotAxis[0] * t,
				  param.spotAxis[2] - param.spotAxis[1] * t,
				};

				Vector3 planeN[] = {
				  Vector3::Cross(param.spotAxis[1], planeTan[0]),
				  Vector3::Cross(planeTan[1], param.spotAxis[0]),
				  Vector3::Cross(planeTan[2], param.spotAxis[1]),
				  Vector3::Cross(param.spotAxis[0], planeTan[3]),
				};

				for (std::size_t i = 0; i < 4; ++i)
					clipPlanes[i] = Plane3::PlaneWithPointOnPlane(param.origin, planeN[i]);
			}

			if (param.type == client::DynamicLightTypeLinear)
				poweredLength = (param.point2 - param.origin).GetSquaredLength();
		}

		bool VulkanDynamicLight::Cull(const AABB3& box) const {
			const client::DynamicLightParam& param = GetParam();

			if (param.type == client::DynamicLightTypeSpotlight) {
				for (const auto& plane : clipPlanes) {
					if (!PlaneCullTest(plane, box))
						return false;
				}
			}

			AABB3 inflatedBox = box.Inflate(param.radius);

			if (param.type == client::DynamicLightTypeLinear) {
				Vector3 intersection;
				if (!OBB3(inflatedBox).RayCast(param.origin, param.point2 - param.origin, &intersection))
					return false;
				return (intersection - param.origin).GetSquaredLength() <= poweredLength;
			}

			return inflatedBox && param.origin;
		}

		bool VulkanDynamicLight::SphereCull(const Vector3& center, float radius) const {
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
	}
}
