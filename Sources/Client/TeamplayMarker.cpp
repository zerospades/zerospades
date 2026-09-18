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

#include "IRenderer.h"
#include "TeamplayMarker.h"

namespace spades {
	namespace client {
		namespace {
			/** Widening a diamond's border by `w` perpendicular to its edges moves each
			 * tip out by `w * sqrt(2)`, since the edges run at 45 degrees. */
			constexpr float kEdgeToTip = 1.41421356F;

			constexpr float kTwoPi = 6.28318531F;

			/** How far the drop shadow sits below the marker, in pixels. */
			constexpr float kShadowOffset = 1.0F;

			/** Outline and rim widths, perpendicular to the edges, as a fraction of the
			 * half size. Both are held to a pixel at least so a small marker keeps
			 * its structure instead of turning into a flat blob. */
			constexpr float kOutlineRatio = 0.14F;
			constexpr float kRimRatio = 0.14F;

			/** The core's half size as a fraction of the body's. */
			constexpr float kCoreRatio = 0.3F;

			/** How far the rim and the core are lifted toward white. */
			constexpr float kRimTint = 0.45F;
			constexpr float kCoreTint = 0.75F;

			/** Intro: the marker pops in from nothing, overshoots and settles. */
			constexpr float kPopDuration = 0.35F;
			constexpr float kPopFadeInDuration = 0.08F;

			/** Pulses: a diamond ring that leaves the marker, grows and fades. A burst
			 * announces a new ping, then a softer pulse keeps an old one findable
			 * without flashing constantly. */
			constexpr float kPulseDuration = 0.75F;
			constexpr float kPulseMaxScale = 2.6F;
			constexpr float kBurstOffsets[] = {0.0F, 0.3F, 0.6F};
			constexpr float kBurstStrength = 1.0F;
			constexpr float kIdlePulseStart = 1.8F;
			constexpr float kIdlePulsePeriod = 1.6F;
			constexpr float kIdlePulseStrength = 0.55F;

			/** Core breathing: the glint brightens and swells slightly, all the time. */
			constexpr float kBreathFrequency = 1.2F; // Hz
			constexpr float kBreathTint = 0.2F;
			constexpr float kBreathScale = 0.12F;

			float OutlineWidth(float halfSize) { return std::max(1.0F, halfSize * kOutlineRatio); }
			float RimWidth(float halfSize) { return std::max(1.0F, halfSize * kRimRatio); }

			Vector3 TowardWhite(const Vector3& c, float amount) {
				return c + (MakeVector3(1, 1, 1) - c) * amount;
			}

			Vector4 Premultiplied(const Vector3& c, float alpha) {
				return MakeVector4(c.x * alpha, c.y * alpha, c.z * alpha, alpha);
			}

			float EaseOutCubic(float t) {
				float u = 1.0F - t;
				return 1.0F - u * u * u;
			}

			/** Ease-out with overshoot: passes 1 and comes back to settle on it. */
			float EaseOutBack(float t) {
				constexpr float c1 = 1.70158F;
				constexpr float c3 = c1 + 1.0F;
				float u = t - 1.0F;
				return 1.0F + c3 * u * u * u + c1 * u * u;
			}

			/** A filled diamond is a parallelogram, which the renderer draws natively
			 * from three of its corners. */
			void FillDiamond(IRenderer& renderer, const Vector2& c, float halfSize,
							 const Vector4& premultipliedColor) {
				if (halfSize <= 0.0F || premultipliedColor.w <= 0.0F)
					return;

				renderer.SetColorAlphaPremultiplied(premultipliedColor);
				renderer.DrawImage(nullptr, MakeVector2(c.x, c.y - halfSize),
								   MakeVector2(c.x + halfSize, c.y),
								   MakeVector2(c.x - halfSize, c.y), AABB2(0, 0, 1, 1));
			}

			/**
			 * A diamond outline of stroke width `width`, centred on the diamond of
			 * `halfSize`. Adjacent edges are perpendicular, so pulling every stroke back
			 * by half the width at its start lets the next stroke fill the corner square
			 * exactly: a mitred join with no overlap to double up the alpha.
			 */
			void StrokeDiamond(IRenderer& renderer, const Vector2& c, float halfSize,
							   float width, const Vector4& premultipliedColor) {
				if (halfSize <= 0.0F || width <= 0.0F || premultipliedColor.w <= 0.0F)
					return;

				const Vector2 corners[4] = {
				  MakeVector2(c.x, c.y - halfSize),
				  MakeVector2(c.x + halfSize, c.y),
				  MakeVector2(c.x, c.y + halfSize),
				  MakeVector2(c.x - halfSize, c.y),
				};

				renderer.SetColorAlphaPremultiplied(premultipliedColor);
				for (int i = 0; i < 4; i++) {
					Vector2 a = corners[i];
					Vector2 b = corners[(i + 1) % 4];
					Vector2 dir = (b - a).Normalize();
					Vector2 back = dir * (width * 0.5F);
					Vector2 n = MakeVector2(-dir.y, dir.x) * (width * 0.5F);
					a -= back;
					b -= back;
					renderer.DrawImage(nullptr, a - n, b - n, a + n, AABB2(0, 0, 1, 1));
				}
			}

			/** One expanding ring, `t` seconds after it left the marker. */
			void DrawPulse(IRenderer& renderer, const Vector2& c, float halfSize,
						   const Vector3& color, float alpha, float t, float strength) {
				if (t < 0.0F || t >= kPulseDuration)
					return;

				float p = t / kPulseDuration;
				float grow = EaseOutCubic(p);
				float size = halfSize * (1.0F + (kPulseMaxScale - 1.0F) * grow);
				float fade = (1.0F - p) * (1.0F - p) * strength * alpha;

				// Thick while it is close to the marker, a hairline as it dissolves.
				float width = std::max(1.0F, halfSize * 0.3F * (1.0F - p));

				StrokeDiamond(renderer, c, size, width + 2.0F,
							  Premultiplied(MakeVector3(0, 0, 0), 0.35F * fade));
				StrokeDiamond(renderer, c, size, width,
							  Premultiplied(TowardWhite(color, kRimTint), fade));
			}
		} // namespace

		float GetPingDiamondExtent(float halfSize) {
			return halfSize + OutlineWidth(halfSize) * kEdgeToTip + kShadowOffset;
		}

		void DrawPingDiamond(IRenderer& renderer, Vector2 center, float halfSize,
							 const Vector3& color, float alpha, float age) {
			if (halfSize <= 0.0F || alpha <= 0.0F)
				return;

			age = std::max(0.0F, age);

			// Diamond tips on pixel centres keep both diagonals symmetric.
			center.x = std::floor(center.x) + 0.5F;
			center.y = std::floor(center.y) + 0.5F;

			// Pulses go underneath, so the marker always reads on top of its own rings.
			for (float offset : kBurstOffsets)
				DrawPulse(renderer, center, halfSize, color, alpha, age - offset,
						  kBurstStrength);
			if (age >= kIdlePulseStart) {
				float t = std::fmod(age - kIdlePulseStart, kIdlePulsePeriod);
				DrawPulse(renderer, center, halfSize, color, alpha, t, kIdlePulseStrength);
			}

			// The intro scales the whole marker, the outline included, so it grows as
			// one piece rather than its layers arriving separately.
			if (age < kPopDuration) {
				halfSize *= EaseOutBack(age / kPopDuration);
				alpha *= std::min(1.0F, age / kPopFadeInDuration);
				if (halfSize <= 0.0F || alpha <= 0.0F)
					return;
			}

			float breath = 0.5F + 0.5F * std::sin(age * kTwoPi * kBreathFrequency);

			const float outlineHalf = halfSize + OutlineWidth(halfSize) * kEdgeToTip;
			const float bodyHalf = halfSize - RimWidth(halfSize) * kEdgeToTip;
			const float coreHalf = halfSize * kCoreRatio * (1.0F + kBreathScale * breath);
			const float coreTint = std::min(1.0F, kCoreTint + kBreathTint * breath);

			const Vector3 black = MakeVector3(0, 0, 0);

			// Back to front: shadow, outline, rim, body, core.
			FillDiamond(renderer, center + MakeVector2(0.0F, kShadowOffset),
						outlineHalf + 0.5F, Premultiplied(black, 0.35F * alpha));
			FillDiamond(renderer, center, outlineHalf, Premultiplied(black, 0.85F * alpha));
			FillDiamond(renderer, center, halfSize,
						Premultiplied(TowardWhite(color, kRimTint), alpha));
			FillDiamond(renderer, center, bodyHalf, Premultiplied(color, alpha));

			// Below a pixel the core would only shimmer as the marker moves.
			if (coreHalf >= 1.0F)
				FillDiamond(renderer, center, coreHalf,
							Premultiplied(TowardWhite(color, coreTint), alpha));
		}
	} // namespace client
} // namespace spades
