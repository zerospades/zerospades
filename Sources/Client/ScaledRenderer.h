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

#pragma once

#include "IRenderer.h"

namespace spades {
	namespace client {
		/**
		 * Scales all 2D drawing (menus, HUD, text) by the UI scale.
		 *
		 * The UI and HUD are laid out in fixed pixel sizes, so on a high resolution
		 * screen they shrink to a fraction of their intended physical size. This proxy
		 * sits between them and the real renderer: it reports a smaller "virtual"
		 * screen (`ScreenWidth()` / `ScreenHeight()` divided by the scale) and maps
		 * every 2D coordinate, clip and scene viewport back to real pixels. Callers
		 * need no changes. Fonts ask `ScreenPixelRatio()` to rasterize glyphs at the real
		 * pixel density, so text stays sharp instead of being upscaled.
		 *
		 * The scale is `ui_scale` (a user multiplier, 1 by default) times an automatic
		 * factor of the window's size against a 1920x1080 reference in quarter steps,
		 * rounded to eighths: a 4K screen gets 2x out of the box, 1440p 1.25x, 1080p and
		 * smaller 1x. It never makes the virtual screen smaller than 800x600, which the
		 * menus need. It is re-evaluated between frames (`Flip`).
		 */
		class ScaledRenderer final : public IRenderer {
			Handle<IRenderer> base;
			float scale = 1.0F;
			float lastWidth = 0.0F, lastHeight = 0.0F;
			float lastUserScale = 0.0F;

			void UpdateScale();

			Vector2 P(const Vector2& v) const { return v * scale; }
			AABB2 R(const AABB2& r) const;

		protected:
			~ScaledRenderer() override;

		public:
			explicit ScaledRenderer(Handle<IRenderer> base);

			/** The range `ui_scale` is held to (the preferences slider's range). */
			static const float kMinUserScale, kMaxUserScale;

			/** The scale for a window of `width`x`height` real pixels. */
			static float ComputeScale(float width, float height, float userScale);

			IRenderer& GetBase() { return *base; }

			/** The UI scale on top of whatever the wrapped renderer has. */
			float ScreenPixelRatio() override { return base->ScreenPixelRatio() * scale; }

			void Init() override;
			void Shutdown() override { base->Shutdown(); }

			Handle<IImage> RegisterImage(const char* filename) override {
				return base->RegisterImage(filename);
			}
			Handle<IModel> RegisterModel(const char* filename) override {
				return base->RegisterModel(filename);
			}
			void ClearCache() override { base->ClearCache(); }
			Handle<IImage> CreateImage(Bitmap& bmp) override { return base->CreateImage(bmp); }
			Handle<IModel> CreateModel(VoxelModel& m) override { return base->CreateModel(m); }

			void SetGameMap(stmp::optional<GameMap&> map) override { base->SetGameMap(map); }
			void SetFogDistance(float d) override { base->SetFogDistance(d); }
			void SetFogColor(Vector3 c) override { base->SetFogColor(c); }

			void StartScene(const SceneDefinition&) override;
			void AddLight(const DynamicLightParam& light) override { base->AddLight(light); }
			void RenderModel(IModel& m, const ModelRenderParam& p) override {
				base->RenderModel(m, p);
			}
			void AddDebugLine(Vector3 a, Vector3 b, Vector4 color) override {
				base->AddDebugLine(a, b, color);
			}
			void AddSprite(IImage& img, Vector3 center, float radius, float rotation) override {
				base->AddSprite(img, center, radius, rotation);
			}
			void AddLongSprite(IImage& img, Vector3 p1, Vector3 p2, float radius) override {
				base->AddLongSprite(img, p1, p2, radius);
			}
			void EndScene() override { base->EndScene(); }

			void MultiplyScreenColor(Vector3 c) override { base->MultiplyScreenColor(c); }
			void SetColor(Vector4 c) override { base->SetColor(c); }
			void SetColorAlphaPremultiplied(Vector4 c) override {
				base->SetColorAlphaPremultiplied(c);
			}

			void DrawImage(stmp::optional<IImage&>, const Vector2& outTopLeft) override;
			void DrawImage(stmp::optional<IImage&>, const AABB2& outRect) override;
			void DrawImage(stmp::optional<IImage&>, const Vector2& outTopLeft,
			               const AABB2& inRect) override;
			void DrawImage(stmp::optional<IImage&>, const AABB2& outRect,
			               const AABB2& inRect) override;
			void DrawImage(stmp::optional<IImage&>, const Vector2& outTopLeft,
			               const Vector2& outTopRight, const Vector2& outBottomLeft,
			               const AABB2& inRect) override;

			void DrawFlatGameMap(const AABB2& outRect, const AABB2& inRect) override {
				base->DrawFlatGameMap(R(outRect), inRect);
			}
			void DrawFlatGameMap(const Vector2& outTopLeft, const Vector2& outTopRight,
			                     const Vector2& outBottomLeft, const AABB2& inRect) override {
				base->DrawFlatGameMap(P(outTopLeft), P(outTopRight), P(outBottomLeft), inRect);
			}

			void BeginClippingCircle(const Vector2& center, float radius) override {
				base->BeginClippingCircle(P(center), radius * scale);
			}
			void EndClippingCircle() override { base->EndClippingCircle(); }
			void BeginClippingRect(const AABB2& outRect) override {
				base->BeginClippingRect(R(outRect));
			}
			void EndClippingRect() override { base->EndClippingRect(); }

			bool IsRendererSW() const override { return base->IsRendererSW(); }
			RendererInfo GetRendererInfo() override { return base->GetRendererInfo(); }

			void FrameDone() override { base->FrameDone(); }
			void Flip() override;
			Handle<Bitmap> ReadBitmap() override;

			float ScreenWidth() override { return base->ScreenWidth() / scale; }
			float ScreenHeight() override { return base->ScreenHeight() / scale; }

			void DrawFilledRectFade(float x0, float y0, float x1, float y1, Vector4 color0,
			                        Vector4 color1, bool horizontal = false) override;
			void DrawFilledTriangle(const Vector2& v0, const Vector2& v1,
			                        const Vector2& v2) override {
				base->DrawFilledTriangle(P(v0), P(v1), P(v2));
			}
		};
	} // namespace client
} // namespace spades
