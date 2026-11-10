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
			// The window size the UI and HUD were laid out for: up to this, 1:1.
			constexpr float kReferenceWidth = 1920.0F;
			constexpr float kReferenceHeight = 1080.0F;
			// The smallest screen the menus fit in.
			constexpr float kMinWidth = 800.0F;
			constexpr float kMinHeight = 600.0F;
			// The range of ui_scale (the Interface Scale slider's).
			constexpr float kMinUserScale = 0.5F;
			constexpr float kMaxUserScale = 2.0F;
		} // namespace

		ScaledRenderer::ScaledRenderer(Handle<IRenderer> base) : base(std::move(base)) {
			SPADES_MARK_FUNCTION();
			if (!this->base)
				SPInvalidArgument("base");
			// Before Init(): views create their fonts first, and fonts read the scale.
			Update();
		}

		ScaledRenderer::~ScaledRenderer() {}

		float ScaledRenderer::ComputeScale(float width, float height) {
			if (width <= 0.0F || height <= 0.0F)
				return 1.0F;

			float fit = std::min(width / kReferenceWidth, height / kReferenceHeight);
			float automatic = std::max(1.0F, std::floor(fit * 4.0F) / 4.0F);

			float user = ui_scale;
			if (!(user > 0.0F)) // also catches NaN from a mangled config
				user = 1.0F;
			user = std::min(std::max(user, kMinUserScale), kMaxUserScale);

			float largest = std::max(1.0F, std::min(width / kMinWidth, height / kMinHeight));
			return std::min(automatic * user, largest);
		}

		void ScaledRenderer::SetFixedScale(float s) {
			fixedScale = s;
			Update();
		}

		bool ScaledRenderer::Update() {
			float s = fixedScale > 0.0F ? fixedScale
			                            : ComputeScale(base->ScreenWidth(), base->ScreenHeight());
			if (s == scale)
				return false;
			SPLog("UI scale: %g (screen %gx%g, ui_scale %s)", s, base->ScreenWidth(),
			      base->ScreenHeight(), ui_scale.CString());
			scale = s;
			return true;
		}

		void ScaledRenderer::Init() {
			base->Init();
			Update();
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
