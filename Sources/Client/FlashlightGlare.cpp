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

#include "FlashlightGlare.h"
#include "GameConstants.h"
#include "GameMap.h"
#include "IImage.h"
#include "IRenderer.h"

namespace spades {
	namespace client {
		namespace {
			/**
			 * The glare's shape: how the eye spreads a point of light (Spencer et al.,
			 * "Physically-Based Glare Effects for Digital Images", 1995), a narrow peak
			 * with a long power-law tail. It is tone mapped into the texture so that
			 * its centre rolls off rather than clipping into a flat disc, fades to zero
			 * with no slope at the rim, and is dithered against banding in the dark.
			 *
			 * A power law has no scale of its own: more light only makes the same
			 * shape wider, which is how each layer below grows with the light.
			 *
			 * Its alpha at `r`, from 0 at the centre to 1 at the rim, is
			 * `1 - exp(-2 * (0.05 / (r + 0.05))^2 * (1 - r^2)^2)`, plus up to one
			 * level of noise, over white.
			 */
			const char* const kGlareImage = "Gfx/Glare.png";

			/**
			 * One layer of the glare, drawn centred on the lamp. `radius` is in screen
			 * heights for the full light of a lamp pointed at the camera, so that it
			 * looks the same at any resolution.
			 */
			struct GlareLayer {
				float radius;
				float intensity;
				float beamPower; // sharpens its falloff towards the edge of the beam
				float lensGlint; // what is left of it outside the beam, from the lens alone
				float whiteness; // how far towards white the beam's colour is pushed
			};

			/** A wide glow in the beam's colour, and a core washed out to white the
			 * way a light too bright to see the colour of is. */
			const GlareLayer kGlareLayers[] = {
				// radius  int.   beam^  glint  white
				{0.4F, 1.0F, 1.5F, 0.05F, 0.2F}, // glow
				{0.12F, 1.0F, 1.0F, 0.2F, 1.0F}, // core
			};

			/**
			 * How much more the camera takes in from a lamp in total darkness than in
			 * daylight, as an eye or an auto-exposed camera adapts to the dark: a lamp
			 * that is a spark at noon floods the view at night.
			 */
			const float kDarkAdaptation = 8.0F;

			/** How fast the glare eases in and out as the map hides the lamp, per second. */
			const float kVisibilityRate = 30.0F;

			/** Below this a layer is too faint and small to show. */
			const float kMinLayerAmount = 1.0F / 1024.0F;
		} // namespace

		void FlashlightGlare::Update(const Lamp& lamp, const Vector3& viewOrigin,
		                             const GameMap& map, float time) {
			frame.reset();

			const Vector3 lampToViewer = viewOrigin - lamp.position;
			const float distance = lampToViewer.GetLength();
			if (distance < 0.01F)
				return; // the camera is the lamp
			const Vector3 toViewer = lampToViewer / distance;

			// The lens faces the front hemisphere; nothing shows from behind it.
			const float cosAngle = Clamp(Vector3::Dot(toViewer, lamp.direction), -1.0F, 1.0F);
			if (cosAngle <= 0.0F)
				return;

			// Fog, as the renderer's: by the square of the horizontal distance.
			const float fog =
			  1.0F - std::min(lampToViewer.GetSquaredLength2D() / (float)FOG_DISTANCE_SQ, 1.0F);
			if (fog <= 0.0F)
				return;

			// A lamp inside a block, its owner's face pressed into a wall, shows
			// nothing. It also lets the ray below start from a clear block.
			const IntVector3 lampBlock = lamp.position.Floor();
			if (map.IsSolidWrapped(lampBlock.x, lampBlock.y, lampBlock.z))
				return;

			// Ease towards whether the map hides the lamp, with one ray step per
			// block boundary crossed. After a while without a glare, the large step
			// simply snaps it.
			{
				const int maxSteps = (int)lampToViewer.GetManhattanLength() + 3;
				const GameMap::RayCastResult res = map.CastRay2(lamp.position, toViewer, maxSteps);
				const bool occluded =
				  res.hit && (res.hitPos - lamp.position).GetSquaredLength() < distance * distance;

				const float dt = std::max(time - visibilityTime, 0.0F);
				visibilityTime = time;
				visibility = Mix(visibility, occluded ? 0.0F : 1.0F,
				                 1.0F - std::exp(-dt * kVisibilityRate));
			}

			// The light reaching the camera falls with the square of the distance,
			// taking the beam's reach as the distance at which half of it is left.
			const float reaches = distance / std::max(lamp.reach, 1.0F);
			const float falloff = 1.0F / (1.0F + reaches * reaches);

			Frame f;
			f.position = lamp.position;
			f.color = lamp.color;
			f.reception = lamp.brightness * visibility * fog * falloff;
			f.beam = 1.0F - SmoothStep(std::min(std::acos(cosAngle) / (lamp.coneAngle * 0.5F), 1.0F));
			f.lens = cosAngle;
			if (f.reception > 0.0F)
				frame = f;
		}

		stmp::optional<Vector3> FlashlightGlare::GetPosition() const {
			if (!frame)
				return {};
			return frame->position;
		}

		void FlashlightGlare::Draw(IRenderer& renderer, const Vector2& screenPos,
		                           float ambient) const {
			if (!frame)
				return;

			const float adaptation = Mix(kDarkAdaptation, 1.0F, Clamp(ambient, 0.0F, 1.0F));
			Handle<IImage> image = renderer.RegisterImage(kGlareImage);

			for (const GlareLayer& layer : kGlareLayers) {
				const float weight = std::pow(frame->beam, layer.beamPower) + layer.lensGlint * frame->lens;
				const float amount = frame->reception * adaptation * layer.intensity * weight;
				if (amount < kMinLayerAmount)
					continue;

				// Split the light between size and brightness, like bloom does: the
				// shape widens with it, and only brightens up to the tone mapped peak
				// baked into the texture, beyond which it would clip into a rim.
				const float spread = std::sqrt(amount);
				const float halfSize = layer.radius * spread * renderer.ScreenHeight();
				const Vector3 tint =
				  Mix(frame->color, MakeVector3(1, 1, 1), layer.whiteness) * std::min(spread, 1.0F);

				// Added onto the frame: a zero alpha keeps what is underneath.
				renderer.SetColorAlphaPremultiplied(MakeVector4(tint.x, tint.y, tint.z, 0.0F));
				renderer.DrawImage(*image, AABB2(screenPos.x - halfSize, screenPos.y - halfSize,
				                                 halfSize * 2.0F, halfSize * 2.0F));
			}
		}
	} // namespace client
} // namespace spades
