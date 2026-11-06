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
		 * The UI and HUD are laid out in fixed sizes, made for a 1080p-class window;
		 * on a larger one they shrink to a fraction of their intended physical size.
		 * This proxy sits between them and the real renderer: it reports a smaller
		 * screen (`ScreenWidth` / `ScreenHeight` divided by the scale) and maps every
		 * 2D coordinate, clip and scene viewport back, so callers need no changes.
		 * `ScreenPixelRatio` and `UIScale` include the scale, so text and other
		 * pixel-exact drawing are done at the screen's real density.
		 *
		 * The wrapped renderer works in window units (points on a high-DPI display),
		 * so the scale is about the window's size as the OS presents it, never about
		 * pixel density, which the wrapped renderer handles.
		 */
		class ScaledRenderer final : public IRenderer {
			Handle<IRenderer> base;
			float scale = 1.0F;      // window units per UI unit
			float fixedScale = 0.0F; // set by SetFixedScale; 0 = automatic

			Vector2 P(const Vector2& v) const { return v * scale; }
			AABB2 R(const AABB2& r) const;

		protected:
			~ScaledRenderer() override;

		public:
			explicit ScaledRenderer(Handle<IRenderer> base);

			/**
			 * The scale for a window of `width`x`height` window units. Its automatic
			 * part is the window's size against 1920x1080, in quarter steps (so the UI
			 * doesn't rescale continuously as the window is resized) and never below 1;
			 * it is multiplied by `ui_scale`, and held so the UI keeps at least 800x600.
			 * A 4K screen gets 2, 1440p 1.25, 1080p and smaller 1.
			 */
			static float ComputeScale(float width, float height);

			/**
			 * For a view laid out at a fixed size (the setup window, whose window is
			 * sized to match): use `s` instead of the automatic scale.
			 */
			void SetFixedScale(float s);

			/**
			 * Re-evaluates the scale from the window size and `ui_scale`. The runner
			 * calls it once per frame, before the view runs, so a whole frame is laid
			 * out at one scale. Returns whether the scale changed (the UI then sees a
			 * new screen size and rebuilds itself).
			 */
			bool Update();

			float ScreenPixelRatio() override { return base->ScreenPixelRatio() * scale; }
			float UIScale() override { return base->UIScale() * scale; }

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
			Handle<IImage> CreateImageWithoutMipmaps(Bitmap& bmp) override {
				return base->CreateImageWithoutMipmaps(bmp);
			}
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
			void Flip() override { base->Flip(); }
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
