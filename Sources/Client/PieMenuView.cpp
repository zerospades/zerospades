/*
 Copyright (c) 2026 Francois ND
 based on code of OpenSpades (c) yvt 2013.

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

#include "PieMenuView.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Client.h"
#include "IFont.h"
#include "IRenderer.h"
#include <Core/Strings.h>

namespace spades {
	namespace client {

		namespace {
			constexpr float kDeadZone = 66.0F;
			constexpr float kRingInner = 77.0F;
			constexpr float kRingOuter = 176.0F;
			constexpr float kSliceGapDeg = 4.0F;
			constexpr float kLabelRadius = 126.5F;

			// Normalized quarter-circle half-width table: table[i] = sqrt(1-(i/N)^2).
			// Built once at program startup; radius-agnostic.
			constexpr int kHalfWidthN = 1024;
			const std::array<float, kHalfWidthN + 1> kHalfWidthTable = []() {
				std::array<float, kHalfWidthN + 1> t{};
				for (int i = 0; i <= kHalfWidthN; i++) {
					float v = static_cast<float>(i) / static_cast<float>(kHalfWidthN);
					float s = 1.0F - v * v;
					t[static_cast<size_t>(i)] = (s > 0.0F) ? sqrtf(s) : 0.0F;
				}
				return t;
			}();

			// sqrt(r*r - y*y) via linear interp of the normalized table.
			float CircleHalfWidth(float yAbs, float r) {
				if (r <= 0.0F || yAbs >= r)
					return 0.0F;
				float t = yAbs / r;
				float idxf = t * static_cast<float>(kHalfWidthN);
				int i = static_cast<int>(idxf);
				if (i >= kHalfWidthN)
					return 0.0F;
				float frac = idxf - static_cast<float>(i);
				float w = kHalfWidthTable[static_cast<size_t>(i)]
				        + (kHalfWidthTable[static_cast<size_t>(i + 1)]
				           - kHalfWidthTable[static_cast<size_t>(i)]) * frac;
				return w * r;
			}

			void DrawDiscFill(IRenderer& r, Vector2 center, float rOut) {
				int yLo = static_cast<int>(floorf(-rOut));
				int yHi = static_cast<int>(ceilf(rOut));
				for (int y = yLo; y < yHi; y++) {
					float yf = static_cast<float>(y) + 0.5F;
					float w = CircleHalfWidth(fabsf(yf), rOut);
					if (w <= 0.0F)
						continue;
					r.DrawImage(nullptr, AABB2(center.x - w, center.y + static_cast<float>(y),
					                           2.0F * w, 1.0F));
				}
			}

			void DrawAnnulusFill(IRenderer& r, Vector2 center, float rIn, float rOut) {
				if (rIn <= 0.0F) {
					DrawDiscFill(r, center, rOut);
					return;
				}
				int yLo = static_cast<int>(floorf(-rOut));
				int yHi = static_cast<int>(ceilf(rOut));
				for (int y = yLo; y < yHi; y++) {
					float yf = static_cast<float>(y) + 0.5F;
					float yAbs = fabsf(yf);
					float wOut = CircleHalfWidth(yAbs, rOut);
					if (wOut <= 0.0F)
						continue;
					if (yAbs < rIn) {
						float wIn = CircleHalfWidth(yAbs, rIn);
						float strip = wOut - wIn;
						if (strip > 0.0F) {
							r.DrawImage(nullptr,
							            AABB2(center.x - wOut,
							                  center.y + static_cast<float>(y),
							                  strip, 1.0F));
							r.DrawImage(nullptr,
							            AABB2(center.x + wIn,
							                  center.y + static_cast<float>(y),
							                  strip, 1.0F));
						}
					} else {
						r.DrawImage(nullptr, AABB2(center.x - wOut,
						                           center.y + static_cast<float>(y),
						                           2.0F * wOut, 1.0F));
					}
				}
			}

			// Annulus ∩ wedge. Rays at θ_c ± α are encoded as (s1,c1) and (s2,c2).
			// Inside-wedge half-planes:   -x·s1 + y·c1 ≥ 0   and   x·s2 - y·c2 ≥ 0.
			void DrawSliceFill(IRenderer& r, Vector2 center, float rIn, float rOut,
			                   float s1, float c1, float s2, float c2) {
				int yLo = static_cast<int>(floorf(-rOut));
				int yHi = static_cast<int>(ceilf(rOut));
				const float kInf = std::numeric_limits<float>::infinity();
				for (int y = yLo; y < yHi; y++) {
					float yf = static_cast<float>(y) + 0.5F;
					float yAbs = fabsf(yf);
					float wOut = CircleHalfWidth(yAbs, rOut);
					if (wOut <= 0.0F)
						continue;
					float wIn = (yAbs < rIn) ? CircleHalfWidth(yAbs, rIn) : 0.0F;

					// Clip by wedge rays.
					float xLoW = -kInf, xHiW = kInf;
					if (s1 > 0.0F) {
						xHiW = std::min(xHiW, yf * c1 / s1);
					} else if (s1 < 0.0F) {
						xLoW = std::max(xLoW, yf * c1 / s1);
					} else if (yf * c1 < 0.0F) {
						continue;
					}
					if (s2 > 0.0F) {
						xLoW = std::max(xLoW, yf * c2 / s2);
					} else if (s2 < 0.0F) {
						xHiW = std::min(xHiW, yf * c2 / s2);
					} else if (yf * c2 < 0.0F) {
						continue;
					}
					if (xHiW <= xLoW)
						continue;

					auto emit = [&](float xA, float xB) {
						float xLo = std::max(xA, xLoW);
						float xHi = std::min(xB, xHiW);
						if (xHi > xLo) {
							r.DrawImage(nullptr,
							            AABB2(center.x + xLo,
							                  center.y + static_cast<float>(y),
							                  xHi - xLo, 1.0F));
						}
					};
					if (yAbs < rIn) {
						emit(-wOut, -wIn);
						emit(wIn, wOut);
					} else {
						emit(-wOut, wOut);
					}
				}
			}
		} // namespace

		PieMenuView::PieMenuView(Client* c, IFont* f, IFont* big)
		    : renderer(c->GetRenderer()), font(f), bigFont(big) {
			// Slice order: top, then clockwise. Affirmative/Negative occupy
			// the same slots in both variants so the gesture transfers.
			worldLabels = {
				_Tr("Client", "Affirmative"),
				_Tr("Client", "Negative"),
				_Tr("Client", "Tear It Down!"),
				_Tr("Client", "Help Me Build"),
				_Tr("Client", "Spawnkiller!"),
				_Tr("Client", "Behind Us!"),
			};
			playerLabels = {
				_Tr("Client", "Affirmative"),
				_Tr("Client", "Negative"),
				_Tr("Client", "Thank You"),
				_Tr("Client", "Cover Me"),
				_Tr("Client", "Behind You"),
				_Tr("Client", "Help Me"),
			};

			const float kPI = static_cast<float>(M_PI);
			const float kSliceSpan = kPI * 2.0F / static_cast<float>(kSliceCount);
			const float halfSliceRad =
				kSliceSpan * 0.5F - (kSliceGapDeg * kPI / 180.0F) * 0.5F;
			for (int i = 0; i < kSliceCount; i++) {
				float center =
					-kPI * 0.5F + kSliceSpan * static_cast<float>(i);
				sliceCenterAngles[i] = center;
				float t1 = center - halfSliceRad;
				float t2 = center + halfSliceRad;
				sliceRays[i] = {sinf(t1), cosf(t1), sinf(t2), cosf(t2)};
			}
		}

		PieMenuView::~PieMenuView() {}

		void PieMenuView::Open(Variant v, int tgtId) {
			open = true;
			variant = v;
			targetPlayerId = tgtId;
			cursor = {0.0F, 0.0F};
			selection = None;
			openPhase = 0.0F;
			highlight.fill(0.0F);
		}

		int PieMenuView::Close() {
			int result = selection;
			open = false;
			selection = None;
			targetPlayerId = -1;
			cursor = {0.0F, 0.0F};
			openPhase = 0.0F;
			highlight.fill(0.0F);
			return result;
		}

		void PieMenuView::Update(float dt) {
			if (!open)
				return;

			constexpr float kOpenRate = 1.0F / 0.12F;
			constexpr float kHighlightUpRate = 1.0F / 0.10F;
			constexpr float kHighlightDownRate = 1.0F / 0.15F;

			openPhase = std::min(1.0F, openPhase + dt * kOpenRate);

			for (int i = 0; i < kSliceCount; i++) {
				float target = (selection == i) ? 1.0F : 0.0F;
				float rate = (target > highlight[i]) ? kHighlightUpRate : kHighlightDownRate;
				float delta = dt * rate;
				if (target > highlight[i])
					highlight[i] = std::min(target, highlight[i] + delta);
				else
					highlight[i] = std::max(target, highlight[i] - delta);
			}
		}

		const std::string& PieMenuView::GetSelectionLabel() const {
			static const std::string empty;
			if (selection < 0 || selection >= kSliceCount)
				return empty;
			const auto& labels =
			  (variant == Variant::Player) ? playerLabels : worldLabels;
			return labels[static_cast<size_t>(selection)];
		}

		void PieMenuView::HandleMouseDelta(float dx, float dy) {
			if (!open)
				return;

			cursor.x += dx;
			cursor.y += dy;

			float maxR = kRingOuter + 20.0F;
			float len = sqrtf(cursor.x * cursor.x + cursor.y * cursor.y);
			if (len > maxR) {
				cursor.x *= maxR / len;
				cursor.y *= maxR / len;
				len = maxR;
			}

			if (len < kDeadZone) {
				selection = None;
				return;
			}

			// angle: 0 = up, clockwise. atan2(x, -y) gives that.
			float angle = atan2f(cursor.x, -cursor.y);
			if (angle < 0.0F)
				angle += static_cast<float>(M_PI) * 2.0F;

			// kSliceCount equal slices, top slice centered at angle 0.
			float span = static_cast<float>(M_PI) * 2.0F
				/ static_cast<float>(kSliceCount);
			int idx = static_cast<int>(
				floorf((angle + span * 0.5F) / span)) % kSliceCount;
			selection = idx;
		}

		void PieMenuView::Draw() {
			if (!open)
				return;

			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();
			Vector2 center = {sw * 0.5F, sh * 0.5F};

			const auto& labels =
			  (variant == Variant::Player) ? playerLabels : worldLabels;

			// Ease-out open animation: scale from 0.85 → 1.0, alpha from 0 → 1.
			float eased = 1.0F - (1.0F - openPhase) * (1.0F - openPhase);
			float scale = 0.85F + 0.15F * eased;
			float alpha = eased;

			float rInner = kRingInner * scale;
			float rOuter = kRingOuter * scale;
			float rLabel = kLabelRadius * scale;

			// Backing disc
			renderer.SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.55F * alpha));
			DrawDiscFill(renderer, center, rOuter);

			// Slices
			for (int i = 0; i < kSliceCount; i++) {
				float h = highlight[i];
				float fillA = (0.08F + (0.85F - 0.08F) * h) * alpha;
				renderer.SetColorAlphaPremultiplied(MakeVector4(fillA, fillA, fillA, fillA));
				float rOutSlice = rOuter + 10.0F * h;
				const SliceRay& rr = sliceRays[i];
				DrawSliceFill(renderer, center, rInner, rOutSlice,
				              rr.s1, rr.c1, rr.s2, rr.c2);
			}

			// Outer and inner outline rings (thin)
			renderer.SetColorAlphaPremultiplied(MakeVector4(alpha * 0.5F, alpha * 0.5F,
			                                                alpha * 0.5F, alpha * 0.5F));
			DrawAnnulusFill(renderer, center, rOuter - 1.0F, rOuter);
			DrawAnnulusFill(renderer, center, rInner, rInner + 1.0F);

			// Labels at kLabelRadius along each slice center
			for (int i = 0; i < kSliceCount; i++) {
				float h = highlight[i];
				Vector2 dir = {cosf(sliceCenterAngles[i]), sinf(sliceCenterAngles[i])};
				Vector2 p = center + dir * rLabel;

				const std::string& label = labels[i];
				Vector2 sz = font->Measure(label);
				Vector2 textPos = {p.x - sz.x * 0.5F, p.y - sz.y * 0.5F};

				float textA = (0.85F + 0.15F * h) * alpha;
				Vector4 textColor = MakeVector4(textA, textA, textA, textA);
				Vector4 textShadow = MakeVector4(0, 0, 0, 0.6F * alpha);
				font->DrawShadow(label, textPos, 1.0F, textColor, textShadow);
			}

			// Center readout: currently selected slice label, scaled by its highlight
			if (selection >= 0 && selection < kSliceCount && bigFont) {
				float h = highlight[selection];
				const std::string& center_label = labels[selection];
				Vector2 sz = bigFont->Measure(center_label);
				Vector2 pos = {center.x - sz.x * 0.5F, center.y - sz.y * 0.5F};
				float a = h * alpha;
				Vector4 col = MakeVector4(a, a, a, a);
				Vector4 shd = MakeVector4(0, 0, 0, 0.7F * a);
				bigFont->DrawShadow(center_label, pos, 1.0F, col, shd);
			}
		}
	} // namespace client
} // namespace spades
