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
			// Quarter steps keep most UI edges on whole pixels.
			float fit = std::min(width / kReferenceWidth, height / kReferenceHeight);
			float autoScale = std::max(1.0F, std::floor(fit * 4.0F) / 4.0F);

			if (!(userScale > 0.0F)) // also catches NaN from a mangled config
				userScale = 1.0F;
			userScale = std::min(std::max(userScale, 0.5F), 3.0F);

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
			float s = ComputeScale(w, h, userScale);
			if (s != scale)
				SPLog("UI scale: %g (%gx%g pixels, ui_scale %g)", s, w, h, userScale);
			scale = s;
			lastWidth = w;
			lastHeight = h;
			refreshRequested = false;
		}

		void ScaledRenderer::Init() {
			base->Init();
			UpdateScale();
		}

		void ScaledRenderer::Flip() {
			base->Flip();
			// Between frames, so a whole frame is laid out at a single scale. The UI
			// notices the virtual screen size change and rebuilds itself.
			if (refreshRequested || base->ScreenWidth() != lastWidth ||
			    base->ScreenHeight() != lastHeight)
				UpdateScale();
		}

		Handle<Bitmap> ScaledRenderer::ReadBitmap() { return base->ReadBitmap(); }

		AABB2 ScaledRenderer::R(const AABB2& r) const {
			return AABB2(r.min.x * scale, r.min.y * scale, r.GetWidth() * scale,
			             r.GetHeight() * scale);
		}

		AABB2 ScaledRenderer::Snap(const AABB2& r) const {
			float x0 = std::round(r.min.x * scale);
			float y0 = std::round(r.min.y * scale);
			float x1 = std::round(r.max.x * scale);
			float y1 = std::round(r.max.y * scale);
			// A hairline must not round away to nothing.
			if (r.max.x != r.min.x && x1 == x0)
				x1 = x0 + (r.max.x > r.min.x ? 1.0F : -1.0F);
			if (r.max.y != r.min.y && y1 == y0)
				y1 = y0 + (r.max.y > r.min.y ? 1.0F : -1.0F);
			return AABB2(x0, y0, x1 - x0, y1 - y0);
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
			// Untextured rectangles are the panels, separators and 1px borders: keep
			// their edges on whole pixels. Images keep their exact placement.
			base->DrawImage(img, img ? R(outRect) : Snap(outRect));
		}

		void ScaledRenderer::DrawImage(stmp::optional<IImage&> img, const Vector2& outTopLeft,
		                               const AABB2& inRect) {
			AABB2 out(outTopLeft.x, outTopLeft.y, inRect.GetWidth(), inRect.GetHeight());
			base->DrawImage(img, R(out), inRect);
		}

		void ScaledRenderer::DrawImage(stmp::optional<IImage&> img, const AABB2& outRect,
		                               const AABB2& inRect) {
			base->DrawImage(img, img ? R(outRect) : Snap(outRect), inRect);
		}

		void ScaledRenderer::DrawImage(stmp::optional<IImage&> img, const Vector2& outTopLeft,
		                               const Vector2& outTopRight, const Vector2& outBottomLeft,
		                               const AABB2& inRect) {
			base->DrawImage(img, P(outTopLeft), P(outTopRight), P(outBottomLeft), inRect);
		}

		void ScaledRenderer::DrawFilledRectFade(float x0, float y0, float x1, float y1,
		                                        Vector4 color0, Vector4 color1,
		                                        bool horizontal) {
			AABB2 r = Snap(AABB2(x0, y0, x1 - x0, y1 - y0));
			base->DrawFilledRectFade(r.min.x, r.min.y, r.max.x, r.max.y, color0, color1,
			                         horizontal);
		}
	} // namespace client
} // namespace spades
