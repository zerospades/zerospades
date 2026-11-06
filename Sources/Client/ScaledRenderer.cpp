/*
 Copyright (c) 2026 ZeroSpades contributors

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

#include "ScaledRenderer.h"

#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <Core/Settings.h>

// A multiplier on the automatic UI scale: 1 = automatic, 1.5 = 50% larger than that.
DEFINE_SPADES_SETTING(ui_scale, "1");

namespace spades {
	namespace client {
		namespace {
			// The resolution the UI and HUD were laid out for: anything this size or
			// smaller is drawn 1:1.
			constexpr float kReferenceWidth = 1920.0F;
			constexpr float kReferenceHeight = 1080.0F;
			// The smallest virtual screen the menus fit in.
			constexpr float kMinWidth = 800.0F;
			constexpr float kMinHeight = 600.0F;
		} // namespace

		const float ScaledRenderer::kMinUserScale = 0.5F;
		const float ScaledRenderer::kMaxUserScale = 2.0F;

		ScaledRenderer::ScaledRenderer(Handle<IRenderer> base) : base(std::move(base)) {
			SPADES_MARK_FUNCTION();
			if (!this->base)
				SPInvalidArgument("base");
			// Before Init(): views create their fonts first, and fonts read the scale.
			UpdateScale();
		}

		ScaledRenderer::~ScaledRenderer() {}

		float ScaledRenderer::ComputeScale(float width, float height, float userScale) {
			if (width <= 0.0F || height <= 0.0F)
				return 1.0F;

			// Automatic part: how much bigger than the reference the window is, by its
			// tighter axis (so ultrawide screens scale by height), in quarter steps.
			float fit = std::min(width / kReferenceWidth, height / kReferenceHeight);
			float autoScale = std::max(1.0F, std::floor(fit * 4.0F) / 4.0F);

			if (!(userScale > 0.0F)) // also catches NaN from a mangled config
				userScale = 1.0F;
			userScale = std::min(std::max(userScale, kMinUserScale), kMaxUserScale);

			// The product, in eighths: coarse enough that most UI edges land on whole
			// pixels, fine enough that every slider step (quarters) still differs.
			float s = std::round(autoScale * userScale * 8.0F) / 8.0F;

			// Never shrink the virtual screen below what the menus need.
			float maxScale = std::min(width / kMinWidth, height / kMinHeight);
			s = std::min(s, std::max(maxScale, 1.0F));
			return std::max(s, 0.5F);
		}

		void ScaledRenderer::UpdateScale() {
			float w = base->ScreenWidth();
			float h = base->ScreenHeight();
			float userScale = ui_scale;

			// Judged in window points, the units the OS sizes things in, so a
			// high-DPI display (whose points are already enlarged) isn't enlarged a
			// second time, then converted to the wrapped renderer's units: pixels
			// when it renders a high-DPI window at full resolution, points when it
			// works in points itself (kv6-editor).
			float pw = windowWidth > 0.0F ? windowWidth : w;
			float ph = windowHeight > 0.0F ? windowHeight : h;
			float perPoint = ComputeScale(pw, ph, userScale);
			float baseUnitsPerPoint = pw > 0.0F ? w / pw : 1.0F;
			float s = perPoint * baseUnitsPerPoint;
			// Logged once the window is known (the constructor's first estimate is
			// provisional), and when anything changes.
			if (windowWidth > 0.0F && (s != scale || perPoint != lastPerPoint))
				SPLog("UI scale: %g per point, %g per renderer unit (window %gx%g points, "
				      "renderer %gx%g, ui_scale %g)",
				      perPoint, s, pw, ph, w, h, userScale);
			lastPerPoint = windowWidth > 0.0F ? perPoint : 0.0F;
			scale = s;
			lastWidth = w;
			lastHeight = h;
			lastUserScale = userScale;
		}

		void ScaledRenderer::SetWindowSize(float width, float height) {
			if (width == windowWidth && height == windowHeight)
				return;
			windowWidth = width;
			windowHeight = height;
			UpdateScale();
		}

		void ScaledRenderer::Init() {
			base->Init();
			UpdateScale();
		}

		void ScaledRenderer::Flip() {
			base->Flip();
			// Between frames, so a whole frame is laid out at a single scale. The UI
			// notices the virtual screen size change and rebuilds itself. ui_scale is
			// polled (a plain field read) so a change from the console applies too.
			if (static_cast<float>(ui_scale) != lastUserScale ||
			    base->ScreenWidth() != lastWidth || base->ScreenHeight() != lastHeight)
				UpdateScale();
		}

		Handle<Bitmap> ScaledRenderer::ReadBitmap() { return base->ReadBitmap(); }

		AABB2 ScaledRenderer::R(const AABB2& r) const {
			return AABB2(r.min.x * scale, r.min.y * scale, r.GetWidth() * scale,
			             r.GetHeight() * scale);
		}

		void ScaledRenderer::StartScene(const SceneDefinition& def) {
			SceneDefinition d = def;
			d.viewportLeft = static_cast<int>(std::round(def.viewportLeft * scale));
			d.viewportTop = static_cast<int>(std::round(def.viewportTop * scale));
			d.viewportWidth = static_cast<int>(std::round(def.viewportWidth * scale));
			d.viewportHeight = static_cast<int>(std::round(def.viewportHeight * scale));
			base->StartScene(d);
		}

		void ScaledRenderer::DrawImage(stmp::optional<IImage&> img, const Vector2& outTopLeft) {
			if (!img) {
				base->DrawImage(img, P(outTopLeft));
				return;
			}
			// Drawn at its own size in virtual pixels.
			AABB2 out(outTopLeft.x, outTopLeft.y, img->GetWidth(), img->GetHeight());
			base->DrawImage(img, R(out), AABB2(0, 0, img->GetWidth(), img->GetHeight()));
		}

		void ScaledRenderer::DrawImage(stmp::optional<IImage&> img, const AABB2& outRect) {
			// No snapping to whole pixels: both renderers rasterize flat fills by
			// pixel centres already, and rounding would make sub-pixel rects (a bar
			// shrinking to empty) show up as 1px slivers.
			base->DrawImage(img, R(outRect));
		}

		void ScaledRenderer::DrawImage(stmp::optional<IImage&> img, const Vector2& outTopLeft,
		                               const AABB2& inRect) {
			AABB2 out(outTopLeft.x, outTopLeft.y, inRect.GetWidth(), inRect.GetHeight());
			base->DrawImage(img, R(out), inRect);
		}

		void ScaledRenderer::DrawImage(stmp::optional<IImage&> img, const AABB2& outRect,
		                               const AABB2& inRect) {
			base->DrawImage(img, R(outRect), inRect);
		}

		void ScaledRenderer::DrawImage(stmp::optional<IImage&> img, const Vector2& outTopLeft,
		                               const Vector2& outTopRight, const Vector2& outBottomLeft,
		                               const AABB2& inRect) {
			base->DrawImage(img, P(outTopLeft), P(outTopRight), P(outBottomLeft), inRect);
		}

		void ScaledRenderer::DrawFilledRectFade(float x0, float y0, float x1, float y1,
		                                        Vector4 color0, Vector4 color1,
		                                        bool horizontal) {
			base->DrawFilledRectFade(x0 * scale, y0 * scale, x1 * scale, y1 * scale, color0, color1,
			                         horizontal);
		}
	} // namespace client
} // namespace spades
