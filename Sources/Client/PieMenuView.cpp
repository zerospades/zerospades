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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "PieMenuView.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "Client.h"
#include "IFont.h"
#include "IRenderer.h"
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Core/Strings.h>

SPADES_SETTING(cg_keyAltAttack);

namespace spades {
	namespace client {

		namespace {
			constexpr float kDeadZone = 66.0F;
			constexpr float kRingInner = 77.0F;
			constexpr float kRingOuter = 176.0F;
			constexpr float kSliceGapDeg = 1.0F;
			constexpr float kLabelRadius = 126.5F;
			// How far past kRingOuter a slice reaches when it is fully highlighted.
			constexpr float kHighlightReach = 10.0F;

			// Half the width of a circle of radius r along a line dy from its centre.
			float CircleHalfWidth(float dy, float r) {
				float s = r * r - dy * dy;
				return (s > 0.0F) ? sqrtf(s) : 0.0F;
			}

			// What a shape covers along one horizontal line: at most two intervals,
			// since a line through the hole of a ring splits it in two.
			struct RowSpans {
				int count = 0;
				std::array<float, 2> lo{};
				std::array<float, 2> hi{};

				void Add(float a, float b) {
					if (b > a && count < 2) {
						lo[static_cast<size_t>(count)] = a;
						hi[static_cast<size_t>(count)] = b;
						count++;
					}
				}
			};

			// Lines sampled per pixel row. Coverage across a row is exact, so these
			// only have to resolve edges that run close to horizontal, like the top
			// and bottom of a ring.
			constexpr int kSubRows = 8;
			// Coverage steps a run of pixels is drawn at; neighbours that round to
			// the same step merge into one rect.
			constexpr float kCoverageLevels = 255.0F;

			// Fills a shape with anti-aliased edges. The 2D pass is not multisampled,
			// so a shape drawn as bare rects or triangles comes out stair-stepped;
			// instead each pixel's coverage is worked out here and the pixel drawn
			// with its alpha scaled by it. `spansAt(y)` gives the shape's extent along
			// the horizontal line at screen y, `bounds` must contain the shape, and
			// `color` is premultiplied.
			//
			// Coverage is worked out on the screen's real pixels, not the UI's: with
			// the UI scaled up (ScaledRenderer), a UI pixel spans several real ones,
			// and anti-aliasing at UI resolution would come out as blocky steps.
			// `bounds` and `spansAt` stay in UI coordinates.
			template <class SpanFn>
			void FillShape(IRenderer& r, Vector4 color, const AABB2& uiBounds, SpanFn&& uiSpansAt) {
				const float scale = r.ScreenPixelRatio();
				const float invScale = 1.0F / scale;
				const AABB2 bounds(uiBounds.min.x * scale, uiBounds.min.y * scale,
				                   uiBounds.GetWidth() * scale, uiBounds.GetHeight() * scale);
				auto spansAt = [&](float realY) {
					RowSpans spans = uiSpansAt(realY * invScale);
					for (int k = 0; k < spans.count; k++) {
						spans.lo[static_cast<size_t>(k)] *= scale;
						spans.hi[static_cast<size_t>(k)] *= scale;
					}
					return spans;
				};

				const int px0 = static_cast<int>(floorf(bounds.GetMinX()));
				const int px1 = static_cast<int>(ceilf(bounds.GetMaxX()));
				const int py0 = static_cast<int>(floorf(bounds.min.y));
				const int py1 = static_cast<int>(ceilf(bounds.max.y));
				if (px1 <= px0 || py1 <= py0 || color.w <= 0.0F)
					return;

				const int width = px1 - px0;
				const float widthF = static_cast<float>(width);
				constexpr float kSampleWeight = 1.0F / static_cast<float>(kSubRows);

				// A span's partial end pixels go straight into `partial`; the run of
				// pixels it covers in full goes into `delta` as a difference array,
				// so a row costs its width however many spans and samples land in it.
				std::vector<float> partial(static_cast<size_t>(width) + 1);
				std::vector<float> delta(static_cast<size_t>(width) + 1);

				for (int py = py0; py < py1; py++) {
					int touchedLo = width;
					int touchedHi = 0;

					for (int sub = 0; sub < kSubRows; sub++) {
						float y = static_cast<float>(py) +
								  (static_cast<float>(sub) + 0.5F) * kSampleWeight;
						RowSpans spans = spansAt(y);
						for (int k = 0; k < spans.count; k++) {
							float a = Clamp(spans.lo[static_cast<size_t>(k)] -
											  static_cast<float>(px0), 0.0F, widthF);
							float b = Clamp(spans.hi[static_cast<size_t>(k)] -
											  static_cast<float>(px0), 0.0F, widthF);
							if (b <= a)
								continue;

							int ia = static_cast<int>(floorf(a));
							int ib = static_cast<int>(floorf(b));
							if (ia == ib) {
								partial[static_cast<size_t>(ia)] += (b - a) * kSampleWeight;
							} else {
								partial[static_cast<size_t>(ia)] +=
								  (static_cast<float>(ia + 1) - a) * kSampleWeight;
								delta[static_cast<size_t>(ia + 1)] += kSampleWeight;
								delta[static_cast<size_t>(ib)] -= kSampleWeight;
								if (ib < width)
									partial[static_cast<size_t>(ib)] +=
									  (b - static_cast<float>(ib)) * kSampleWeight;
							}
							touchedLo = std::min(touchedLo, ia);
							touchedHi = std::max(touchedHi, std::min(ib + 1, width));
						}
					}

					// Walk the touched pixels and draw each run of equal coverage.
					// One step past the end closes the last run.
					float running = 0.0F;
					int runStart = touchedLo;
					int runLevel = 0;
					for (int i = touchedLo; i <= touchedHi; i++) {
						int level = 0;
						if (i < touchedHi) {
							running += delta[static_cast<size_t>(i)];
							float cov = Clamp(running + partial[static_cast<size_t>(i)],
											  0.0F, 1.0F);
							level = static_cast<int>(cov * kCoverageLevels + 0.5F);
						}
						if (level == runLevel)
							continue;
						if (runLevel > 0) {
							r.SetColorAlphaPremultiplied(
							  color * (static_cast<float>(runLevel) / kCoverageLevels));
							// One real pixel row, given in UI coordinates.
							r.DrawImage(nullptr,
							            AABB2(static_cast<float>(px0 + runStart) * invScale,
							                  static_cast<float>(py) * invScale,
							                  static_cast<float>(i - runStart) * invScale,
							                  invScale));
						}
						runStart = i;
						runLevel = level;
					}

					// Only what this row touched is dirty; a slice covers a fraction
					// of its bounds, so clearing the whole width would be most of the work.
					if (touchedHi >= touchedLo) {
						std::fill(partial.begin() + touchedLo, partial.begin() + touchedHi + 1, 0.0F);
						std::fill(delta.begin() + touchedLo, delta.begin() + touchedHi + 1, 0.0F);
					}
				}
			}

			void DrawDiscFill(IRenderer& r, Vector4 color, Vector2 center, float rOut) {
				FillShape(r, color,
						  AABB2(center.x - rOut, center.y - rOut, 2.0F * rOut, 2.0F * rOut),
						  [&](float y) {
							  RowSpans spans;
							  float w = CircleHalfWidth(y - center.y, rOut);
							  spans.Add(center.x - w, center.x + w);
							  return spans;
						  });
			}

			void DrawAnnulusFill(IRenderer& r, Vector4 color, Vector2 center, float rIn,
								 float rOut) {
				FillShape(r, color,
						  AABB2(center.x - rOut, center.y - rOut, 2.0F * rOut, 2.0F * rOut),
						  [&](float y) {
							  RowSpans spans;
							  float dy = y - center.y;
							  float wOut = CircleHalfWidth(dy, rOut);
							  float wIn = CircleHalfWidth(dy, rIn);
							  if (wIn > 0.0F) {
								  spans.Add(center.x - wOut, center.x - wIn);
								  spans.Add(center.x + wIn, center.x + wOut);
							  } else {
								  spans.Add(center.x - wOut, center.x + wOut);
							  }
							  return spans;
						  });
			}

			// Annulus ∩ wedge. Rays at θ_c ± α are encoded as (s1,c1) and (s2,c2).
			// Inside-wedge half-planes:   -x·s1 + y·c1 ≥ 0	  and	x·s2 - y·c2 ≥ 0.
			void DrawSliceFill(IRenderer& r, Vector4 color, Vector2 center, float rIn,
							   float rOut, float s1, float c1, float s2, float c2) {
				const float kInf = std::numeric_limits<float>::infinity();
				FillShape(
				  r, color, AABB2(center.x - rOut, center.y - rOut, 2.0F * rOut, 2.0F * rOut),
				  [&](float y) {
					  RowSpans spans;
					  float dy = y - center.y;

					  // Clip by wedge rays.
					  float xLoW = -kInf, xHiW = kInf;
					  if (s1 > 0.0F) {
						  xHiW = std::min(xHiW, dy * c1 / s1);
					  } else if (s1 < 0.0F) {
						  xLoW = std::max(xLoW, dy * c1 / s1);
					  } else if (dy * c1 < 0.0F) {
						  return spans;
					  }
					  if (s2 > 0.0F) {
						  xLoW = std::max(xLoW, dy * c2 / s2);
					  } else if (s2 < 0.0F) {
						  xHiW = std::min(xHiW, dy * c2 / s2);
					  } else if (dy * c2 > 0.0F) {
						  return spans;
					  }

					  auto clipped = [&](float xA, float xB) {
						  spans.Add(center.x + std::max(xA, xLoW),
									center.x + std::min(xB, xHiW));
					  };
					  float wOut = CircleHalfWidth(dy, rOut);
					  float wIn = CircleHalfWidth(dy, rIn);
					  if (wIn > 0.0F) {
						  clipped(-wOut, -wIn);
						  clipped(wIn, wOut);
					  } else {
						  clipped(-wOut, wOut);
					  }
					  return spans;
				  });
			}

			// Binding names are stored verbosely; the hint has to stay readable at
			// the size it is drawn, so the mouse buttons get their common short forms.
			std::string ShortKeyName(const std::string& key) {
				if (EqualsIgnoringCase(key, "LeftMouseButton"))
					return "LMB";
				if (EqualsIgnoringCase(key, "RightMouseButton"))
					return "RMB";
				if (EqualsIgnoringCase(key, "MiddleMouseButton"))
					return "MMB";
				return ToUpperCase(key);
			}

			// How wide a label may run inside its wedge, at the label radius, with
			// some room left on either side.
			constexpr float kLabelMaxWidth = 108.0F;
			// A tooltip is read under the ring, as wide as the ring itself.
			constexpr float kTooltipMaxWidth = 2.0F * kRingOuter;
			// From the ring's furthest reach to the first tooltip line, clear of the
			// page dots below the ring.
			constexpr float kTooltipGap = 26.0F;

			// A slice is styled by what it sends, so a wedge that pings, one that
			// speaks and one that runs a command need not be told apart by reading.
			// FIXME: a leading / says nothing of intent, so two commands are known by
			// name: /pingmark marks a place, and /pm only talks, so it is drawn as
			// chat. Any other command that talks is still drawn as a command.
			Vector3 SliceAccent(const PieSlice& slice) {
				const std::string& t = slice.text;
				if (t.compare(0, 10, "/pingmark ") == 0)
					return MakeVector3(1.0F, 0.78F, 0.3F);
				if (t.compare(0, 1, "/") == 0 && t.compare(0, 4, "/pm ") != 0)
					return MakeVector3(0.5F, 0.78F, 1.0F);
				return MakeVector3(1.0F, 1.0F, 1.0F);
			}

			// Cuts a line short, at a codepoint boundary, so that it fits with an
			// ellipsis after it.
			std::string Ellipsize(IFont& font, const std::string& text, float maxWidth) {
				if (font.Measure(text).x <= maxWidth)
					return text;

				// Searched for rather than walked back one character at a time: a
				// menu can hold sixty labels of 255 bytes, all fitted as it opens.
				std::vector<size_t> bounds; // codepoint starts, past the first
				for (size_t i = 1; i < text.size(); i++)
					if (!IsUTF8Continuation(text[i]))
						bounds.push_back(i);

				auto candidate = [&](size_t n) {
					return TrimSpaces(text.substr(0, n == 0 ? 0 : bounds[n - 1])) + "...";
				};
				size_t lo = 0, hi = bounds.size(); // candidate(lo) is the fallback
				while (lo < hi) {
					size_t mid = (lo + hi + 1) / 2;
					if (font.Measure(candidate(mid)).x <= maxWidth)
						lo = mid;
					else
						hi = mid - 1;
				}
				return candidate(lo);
			}

			// Lays a label out on one line, or two broken at the space that balances
			// them best, cutting short whatever still does not fit.
			std::vector<std::string> FitLines(IFont& font, const std::string& text,
											  float maxWidth) {
				if (text.empty())
					return {};
				if (font.Measure(text).x <= maxWidth)
					return {text};

				size_t bestSplit = std::string::npos;
				float bestWidth = std::numeric_limits<float>::infinity();
				for (size_t i = text.find(' '); i != std::string::npos;
					 i = text.find(' ', i + 1)) {
					float w = std::max(font.Measure(text.substr(0, i)).x,
									   font.Measure(text.substr(i + 1)).x);
					if (w < bestWidth) {
						bestWidth = w;
						bestSplit = i;
					}
				}

				if (bestSplit == std::string::npos)
					return {Ellipsize(font, text, maxWidth)};

				return {Ellipsize(font, TrimSpaces(text.substr(0, bestSplit)), maxWidth),
						Ellipsize(font, TrimSpaces(text.substr(bestSplit + 1)), maxWidth)};
			}

			// A label or a theme came off the wire, so it is made safe to draw: what
			// the font cannot lay out on one line is dropped. Only what is drawn is
			// touched; the Text a slice sends goes out exactly as it came.
			std::string DisplayText(const std::string& text) {
				std::string out;
				out.reserve(text.size());
				for (char c : StripNewlines(text)) {
					auto u = static_cast<unsigned char>(c);
					if (u >= 0x20 && u != 0x7F)
						out += c;
				}
				return TrimSpaces(out);
			}
		} // namespace

		PieMenuView::PieMenuView(Client* c, IFont* f, IFont* big)
			: renderer(c->GetRenderer()), font(f), bigFont(big) {
			const float halfSliceRad = kSliceSpan * 0.5F - DEG2RAD(kSliceGapDeg) * 0.5F;
			for (int i = 0; i < kSliceCount; i++) {
				float center = -kHalfPi + kSliceSpan * static_cast<float>(i);
				sliceCenterAngles[i] = center;
				float t1 = center - halfSliceRad;
				float t2 = center + halfSliceRad;
				sliceRays[i] = {sinf(t1), cosf(t1), sinf(t2), cosf(t2)};
			}
		}

		PieMenuView::~PieMenuView() {}

		const PieMenuView::Page& PieMenuView::CurrentPage() const {
			SPAssert(!pages.empty());
			return pages[static_cast<size_t>(Clamp(page, 0, GetPageCount() - 1))];
		}

		const Pie& PieMenuView::CurrentPie() const { return CurrentPage().pie; }

		void PieMenuView::RestorePage() {
			int remembered = lastPage[static_cast<size_t>(contextIndex)];
			page = std::max(0, std::min(remembered, GetPageCount() - 1));
		}

		void PieMenuView::Open(std::vector<Pie> newPies, PieContext context, int tgtId) {
			SPAssert(!newPies.empty());

			pages.clear();
			pages.reserve(newPies.size());
			for (Pie& pie : newPies) {
				Page p;
				p.theme = DisplayText(pie.theme);
				for (size_t i = 0; i < kSliceCount; i++) {
					// A label that draws as nothing must not hide a slice that
					// sends something: what it sends is drawn instead, and failing
					// that a mark, so the wedge never passes for an empty one.
					const PieSlice& slice = pie.slices[i];
					p.labels[i] = DisplayText(slice.label);
					if (p.labels[i].empty())
						p.labels[i] = DisplayText(slice.text);
					if (p.labels[i].empty() && !slice.IsBlank())
						p.labels[i] = "?";
					p.lines[i] = FitLines(*font, p.labels[i], kLabelMaxWidth);
					p.tooltips[i] = FitLines(*font, DisplayText(slice.tooltip), kTooltipMaxWidth);
				}
				p.pie = std::move(pie);
				pages.push_back(std::move(p));
			}

			// A menu opens on exactly one context, and each has its own slot.
			contextIndex = 0;
			while (contextIndex < kContextCount && context != (1 << contextIndex))
				contextIndex++;
			SPAssert(contextIndex < kContextCount);

			open = true;
			targetPlayerId = tgtId;
			cursor = {0.0F, 0.0F};
			selection = None;
			openPhase = 0.0F;
			pagePhase = 1.0F;
			highlight.fill(0.0F);
			RestorePage();
		}

		int PieMenuView::Close() {
			int result = selection;
			open = false;
			selection = None;
			targetPlayerId = -1;
			cursor = {0.0F, 0.0F};
			page = 0;
			openPhase = 0.0F;
			highlight.fill(0.0F);
			return result;
		}

		void PieMenuView::CyclePage(int dir) {
			if (!open || dir == 0)
				return;

			int count = GetPageCount();
			if (count <= 1)
				return;

			page = ((page + dir) % count + count) % count;
			lastPage[static_cast<size_t>(contextIndex)] = page;
			pagePhase = 0.0F;
			hintNeeded = false;

			// The wedge under the cursor may be empty on the new ring.
			UpdateSelection();
		}

		void PieMenuView::Update(float dt) {
			if (!open)
				return;

			constexpr float kOpenRate = 1.0F / 0.12F;
			constexpr float kPageRate = 1.0F / 0.10F;
			constexpr float kHighlightUpRate = 1.0F / 0.10F;
			constexpr float kHighlightDownRate = 1.0F / 0.15F;

			openPhase = std::min(1.0F, openPhase + dt * kOpenRate);
			pagePhase = std::min(1.0F, pagePhase + dt * kPageRate);
			hintTime += dt;

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

		const PieSlice* PieMenuView::GetSelectedSlice() const {
			if (!open || selection < 0 || selection >= kSliceCount)
				return nullptr;
			return &CurrentPie().slices[static_cast<size_t>(selection)];
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
			}

			UpdateSelection();
		}

		void PieMenuView::UpdateSelection() {
			float len = sqrtf(cursor.x * cursor.x + cursor.y * cursor.y);
			if (len < kDeadZone) {
				selection = None;
				return;
			}

			// angle: 0 = up, clockwise. atan2(x, -y) gives that.
			float angle = atan2f(cursor.x, -cursor.y);
			if (angle < 0.0F)
				angle += kTwoPi;

			// kSliceCount equal slices, top slice centered at angle 0.
			int idx = static_cast<int>(floorf((angle + kSliceSpan * 0.5F) / kSliceSpan)) % kSliceCount;

			// A blank wedge sends nothing, so it is never highlighted as if it would.
			if (CurrentPie().slices[static_cast<size_t>(idx)].IsBlank())
				idx = None;
			selection = idx;
		}

		void PieMenuView::Draw() {
			if (!open)
				return;

			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();
			
			Vector2 center = {sw * 0.5F, sh * 0.5F};

			const auto& labels = CurrentPage().labels;

			// Ease-out open animation: scale from 0.85 → 1.0, alpha from 0 → 1.
			float eased = 1.0F - (1.0F - openPhase) * (1.0F - openPhase);
			float scale = 0.85F + 0.15F * eased;
			float alpha = eased;

			// Labels fade back in after a ring flip so the swap reads as a change.
			float easedPage = 1.0F - (1.0F - pagePhase) * (1.0F - pagePhase);

			float rInner = kRingInner * scale;
			float rOuter = kRingOuter * scale;
			float rLabel = kLabelRadius * scale;

			// Backing disc
			DrawDiscFill(renderer, MakeVector4(0, 0, 0, 0.55F * alpha), center, rOuter);

			// A highlighted slice reaches past the others, so how far out it goes
			// is its own and is asked for in both passes below.
			auto sliceOuter = [&](int i) { return rOuter + kHighlightReach * highlight[i]; };

			// Slices
			for (int i = 0; i < kSliceCount; i++) {
				float h = highlight[i];
				// A blank wedge is never highlighted, and sits back from the rest.
				const PieSlice& slice = CurrentPie().slices[static_cast<size_t>(i)];
				float restA = slice.IsBlank() ? 0.03F : 0.08F;
				float fillA = (restA + (0.85F - restA) * h) * alpha;
				Vector3 accent = SliceAccent(slice);
				const SliceRay& rr = sliceRays[i];
				DrawSliceFill(renderer,
							  MakeVector4(accent.x * fillA, accent.y * fillA, accent.z * fillA,
										  fillA),
							  center, rInner, sliceOuter(i), rr.s1, rr.c1, rr.s2, rr.c2);
			}

			// Outer and inner outline rings (thin). The outer one is drawn slice by
			// slice, along each one's own edge: a single ring across the whole pie
			// would sit at the resting radius and cut over the top of whichever
			// slice had reached past it.
			const float outlineA = alpha * 0.5F;
			const Vector4 outlineColor = MakeVector4(outlineA, outlineA, outlineA, outlineA);
			for (int i = 0; i < kSliceCount; i++) {
				const SliceRay& rr = sliceRays[i];
				float rOutSlice = sliceOuter(i);
				DrawSliceFill(renderer, outlineColor, center, rOutSlice - 1.0F, rOutSlice,
							  rr.s1, rr.c1, rr.s2, rr.c2);
			}
			DrawAnnulusFill(renderer, outlineColor, center, rInner, rInner + 1.0F);

			// Labels at kLabelRadius along each slice center
			for (int i = 0; i < kSliceCount; i++) {
				float h = highlight[i];
				Vector2 dir = {cosf(sliceCenterAngles[i]), sinf(sliceCenterAngles[i])};
				Vector2 p = center + dir * rLabel;

				const auto& lines = CurrentPage().lines[i];
				if (lines.empty())
					continue;

				// A blank slice has no label, so only sending ones reach here.
				float textA = (0.85F + 0.15F * h) * alpha * easedPage;
				Vector3 accent = SliceAccent(CurrentPie().slices[static_cast<size_t>(i)]);
				Vector4 textColor = MakeVector4(accent.x, accent.y, accent.z, textA);
				Vector4 textShadow = MakeVector4(0, 0, 0, 0.6F * textA);

				float lineH = font->Measure("A").y;
				float y = p.y - lineH * static_cast<float>(lines.size()) * 0.5F;
				for (const std::string& line : lines) {
					float w = font->Measure(line).x;
					font->DrawShadow(line, MakeVector2(p.x - w * 0.5F, y), 1.0F, textColor,
									 textShadow);
					y += lineH;
				}
			}

			// Center readout: currently selected slice label, scaled by its highlight
			if (selection >= 0 && selection < kSliceCount && bigFont) {
				float h = highlight[selection];
				const std::string& centerLabel = labels[selection];
				// The hole in the middle is all it has: the big font while it fits,
				// then the small one, cut short if even that does not.
				float maxW = 2.0F * rInner - 12.0F;
				IFont* centerFont = bigFont;
				std::string text = centerLabel;
				if (bigFont->Measure(text).x > maxW) {
					centerFont = font;
					text = Ellipsize(*font, text, maxW);
				}
				Vector2 sz = centerFont->Measure(text);
				Vector2 pos = {center.x - sz.x * 0.5F, center.y - sz.y * 0.5F};
				float a = h * alpha * easedPage;
				Vector4 col = MakeVector4(a, a, a, a);
				Vector4 shd = MakeVector4(0, 0, 0, 0.7F * a);
				centerFont->DrawShadow(text, pos, 1.0F, col, shd);
			} else if (font) {
				// Nothing aimed at yet: name the ring instead, so which one is up
				// can be read at a glance rather than inferred from six labels.
				std::string pageName = Ellipsize(*font, CurrentPage().theme, 2.0F * rInner - 12.0F);
				Vector2 sz = font->Measure(pageName);
				Vector2 pos = {center.x - sz.x * 0.5F, center.y - sz.y * 0.5F};
				float a = alpha * easedPage * 0.55F;
				font->DrawShadow(pageName, pos, 1.0F, MakeVector4(1, 1, 1, a),
								 MakeVector4(0, 0, 0, 0.6F * a));
			}

			DrawPageIndicator(center, rOuter, alpha);

			// The highlighted slice's tooltip, under the ring and its page dots: the
			// hole in the middle is too small for a sentence.
			if (selection >= 0 && selection < kSliceCount && font) {
				const auto& tip = CurrentPage().tooltips[selection];
				float a = highlight[selection] * alpha * easedPage;
				float lineH = font->Measure("A").y;
				float y = center.y + rOuter + kHighlightReach + kTooltipGap;
				for (const std::string& line : tip) {
					float w = font->Measure(line).x;
					font->DrawShadow(line, MakeVector2(center.x - w * 0.5F, y), 1.0F,
									 MakeVector4(0.85F * a, 0.85F * a, 0.85F * a, a),
									 MakeVector4(0, 0, 0, 0.6F * a));
					y += lineH;
				}
			}
		}

		void PieMenuView::DrawPageIndicator(Vector2 center, float rOuter, float alpha) {
			int pageCount = GetPageCount();
			if (pageCount <= 1)
				return;

			constexpr float kDotRadius = 3.0F;
			constexpr float kDotSpacing = 13.0F;
			constexpr float kDotOffset = 16.0F;

			// Pulses only until the player finds the flip, then settles into a
			// quiet position readout that never asks for attention again.
			float pulse = 0.75F + 0.25F * sinf(hintTime * 4.0F);
			float indicatorA = alpha * (hintNeeded ? pulse : 0.4F);

			float dotY = center.y + rOuter + kDotOffset;
			float dotX = center.x - kDotSpacing * static_cast<float>(pageCount - 1) * 0.5F;
			for (int i = 0; i < pageCount; i++) {
				bool active = (i == page);
				float a = indicatorA * (active ? 0.95F : 0.35F);
				DrawDiscFill(renderer, MakeVector4(a, a, a, a),
							 MakeVector2(dotX + kDotSpacing * static_cast<float>(i), dotY),
							 active ? kDotRadius : kDotRadius - 1.0F);
			}

			if (!hintNeeded || !font)
				return;

			std::string hint = _Tr("Client", "{0} More", ShortKeyName(cg_keyAltAttack));
			Vector2 sz = font->Measure(hint);
			Vector2 pos = {center.x - sz.x * 0.5F, dotY + kDotRadius + 6.0F};
			float a = alpha * pulse;
			font->DrawShadow(hint, pos, 1.0F, MakeVector4(1, 1, 1, a),
							 MakeVector4(0, 0, 0, 0.6F * a));
		}
	} // namespace client
} // namespace spades
