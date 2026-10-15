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

#include <algorithm>
#include <cmath>

#include "FlashlightBudget.h"
#include "GameMap.h"
#include <Core/Debug.h>

namespace spades {
	namespace client {
		namespace {
			/** How bright a glow is next to the light a spotlight would bring there:
			 * it is added onto whatever it lands on, whatever that reflects. */
			constexpr float kGlowStrength = 0.4F;

			/** The smallest a glow is drawn, in blocks, so a beam landing right in front
			 * of its lamp still shows. */
			constexpr float kMinGlowRadius = 0.3F;

			/** How far off the block it lands on a glow is drawn, in blocks, so that the
			 * block doesn't cut it. */
			constexpr float kGlowLift = 0.1F;

			/** Whether a sphere is at least partly in the view's frustum. */
			bool IsInView(const SceneDefinition& view, const Vector3& center, float radius) {
				const Vector3 d = center - view.viewOrigin;
				const float x = Vector3::Dot(d, view.viewAxis[0]);
				const float y = Vector3::Dot(d, view.viewAxis[1]);
				const float z = Vector3::Dot(d, view.viewAxis[2]);
				if (z < -radius || z > view.zFar + radius)
					return false;

				const float tanX = std::tan(view.fovX * 0.5F);
				const float tanY = std::tan(view.fovY * 0.5F);
				// How far outside each pair of side planes the centre is
				if ((std::fabs(x) - z * tanX) / std::sqrt(1.0F + tanX * tanX) > radius)
					return false;
				if ((std::fabs(y) - z * tanY) / std::sqrt(1.0F + tanY * tanY) > radius)
					return false;
				return true;
			}
		} // namespace

		void FlashlightBudget::Submit(IRenderer& renderer, const GameMap& map,
		                              const SceneDefinition& view) {
			SPADES_MARK_FUNCTION();

			// The camera's own beam first, then those lighting the view, nearest
			// first; a beam lights nothing farther from its lamp than its reach.
			std::vector<float> priority(beams.size());
			order.clear();
			for (std::size_t i = 0; i < beams.size(); i++) {
				const DynamicLightParam& light = beams[i].light;
				if (beams[i].viewed)
					priority[i] = -1.0F;
				else if (IsInView(view, light.origin, light.radius))
					priority[i] = (light.origin - view.viewOrigin).GetLength();
				else
					continue; // nothing of it to show
				order.push_back(i);
			}

			// Stable, so that lamps as near as each other keep their order from frame
			// to frame and don't trade places.
			std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
				return priority[a] < priority[b];
			});

			for (std::size_t k = 0; k < order.size(); k++) {
				const Beam& beam = beams[order[k]];
				if (k < MaxLitBeams)
					renderer.AddLight(beam.light);
				else
					AddGlow(renderer, map, beam);
			}
			beams.clear();
		}

		void FlashlightBudget::AddGlow(IRenderer& renderer, const GameMap& map,
		                               const Beam& beam) {
			const DynamicLightParam& light = beam.light;
			const Vector3 axis = light.spotAxis[2].Normalize();
			const float reach = light.radius;

			// Where the beam's axis meets the first block in its way, within its reach
			const GameMap::RayCastResult hit =
			  map.CastRay2(light.origin, axis, static_cast<int>(reach * 3.0F) + 3);
			if (!hit.hit || hit.startSolid)
				return;
			const float distance = (hit.hitPos - light.origin).GetLength();
			if (distance >= reach)
				return;

			// Falling off over its reach as the spotlight does, and as wide as its cone
			// is where it lands
			const float reachLeft = 1.0F - distance / reach;
			const Vector3 color = beam.glowColor * (reachLeft * reachLeft * kGlowStrength);
			const float radius =
			  std::max(distance * std::tan(light.spotAngle * 0.5F), kMinGlowRadius);
			const Vector3 position =
			  hit.hitPos + MakeVector3(hit.normal.x, hit.normal.y, hit.normal.z) * kGlowLift;

			// Added onto the scene: a zero alpha keeps what is underneath.
			Handle<IImage> image = renderer.RegisterImage("Gfx/Ball.png");
			renderer.SetColorAlphaPremultiplied(MakeVector4(color.x, color.y, color.z, 0.0F));
			renderer.AddSprite(*image, position, radius, 0.0F);
		}
	} // namespace client
} // namespace spades
