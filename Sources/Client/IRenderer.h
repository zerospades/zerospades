/*
 Copyright (c) 2013 yvt

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

#include <array>

#include "IImage.h"
#include "IModel.h"
#include "SceneDefinition.h"
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	class Bitmap;
	class VoxelModel;
	namespace client {
		class GameMap;

		struct ModelRenderParam {
			/** The transformatrix matrix applied on the model. */
			Matrix4 matrix = Matrix4::Identity();

			/** Voxels having a color value `(0, 0, 0)` are replaced with
			 * this color. */
			Vector3 customColor = MakeVector3(0, 0, 0);

			/** Specifies to render the model in front of other non-depth-hack
			 * models. Useful for first-person models. */
			bool depthHack = false;

			/** Specifies whether the model casts a shadow. */
			bool castShadow = true;

			/**
			 * Specifies that the model is not an actual object in the virtual world, thus does not
			 * affect the shading of other objects and does not appear in a mirror.
			 *
			 * This excludes the model from visual effects such as shadowing, global illumination
			 * (specifically, screen-space ambient occlusion, ATM), and dynamic lighting.
			 * In exchange, it allows the use of an opacity value other than `1`.
			 *
			 * `ghost` implies `!castShadow`.
			 */
			bool ghost = false;

			/** Specifies the opacity of the model. Ignored if `ghost` is `false`. */
			float opacity = 1.0F;

			/**
			 * Draws the model again where it is hidden from the camera — behind world
			 * geometry — as a lit, tinted shape, and leaves it alone where it is in
			 * plain sight. Used to reveal a player through walls.
			 *
			 * This is drawn in addition to the model's normal appearance, not instead of
			 * it. Backends that do not implement it ignore the flag.
			 */
			bool xray = false;

			/** The colour the model is tinted with where `xray` reveals it. */
			Vector3 xrayColor = MakeVector3(1, 1, 1);
		};

		enum DynamicLightType {
			DynamicLightTypePoint,
			DynamicLightTypeSpotlight,
			DynamicLightTypeLinear
		};

		struct DynamicLightParam {
			DynamicLightType type = DynamicLightTypePoint;

			/** The position of the light. */
			Vector3 origin;

			/** The effective radius of the light. Objects outside this radius
			 * is unaffected by the light. */
			float radius;

			Vector3 color;

			/**
			 * The second position of the light.
			 *
			 * For `DyanmicLightTypeLinear`, this specifies the second endpoint's position. For
			 * other light types, this value is ignored.
			 */
			Vector3 point2;

			/** The basis vectors specifying the orientation of a spotlight.
			 * See the existing code for usage. */
			std::array<Vector3, 3> spotAxis;

			/** The projected image for a spotlight. */
			IImage* image = nullptr; // TODO: Replace this raw pointer with something

			float spotAngle = 0.0F;

			/** When set to `true`, the lens flare post-effect is enabled for
			 * the light. */
			bool useLensFlare = false;
		};

		class IRenderer : public RefCountedObject {
		protected:
			virtual ~IRenderer() {}

		public:
			IRenderer() {}

			virtual void Init() = 0;
			virtual void Shutdown() = 0;

			virtual Handle<IImage> RegisterImage(const char* filename) = 0;
			virtual Handle<IModel> RegisterModel(const char* filename) = 0;

			/**
			 * Clear the cache of models and images loaded via `RegisterModel`
			 * and `RegisterImage`. This method is merely a hint - the
			 * implementation may partially or completely ignore the request.
			 */
			virtual void ClearCache() {}

			virtual Handle<IImage> CreateImage(Bitmap&) = 0;
			virtual Handle<IModel> CreateModel(VoxelModel&) = 0;

			virtual void SetGameMap(stmp::optional<GameMap&>) = 0;
			virtual void SetFogDistance(float) = 0;
			virtual void SetFogColor(Vector3) = 0;

			/** Starts rendering a scene and waits for additional objects. */
			virtual void StartScene(const SceneDefinition&) = 0;

			virtual void AddLight(const client::DynamicLightParam& light) = 0;

			virtual void RenderModel(IModel&, const ModelRenderParam&) = 0;

			virtual void AddDebugLine(Vector3 a, Vector3 b, Vector4 color) = 0;

			virtual void AddSprite(IImage&, Vector3 center, float radius, float rotation) = 0;
			virtual void AddLongSprite(IImage&, Vector3 p1, Vector3 p2, float radius) = 0;

			/** Finalizes a scene. 2D drawing follows. */
			virtual void EndScene() = 0;

			virtual void MultiplyScreenColor(Vector3) = 0;

			/** Sets color for image drawing. Deprecated because
			 * some methods treats this as an alpha premultiplied, while
			 * others treats this as an alpha non-premultiplied.
			 * @deprecated */
			virtual void SetColor(Vector4) = 0;
			/** Sets color for image drawing. Always alpha premultiplied. */
			virtual void SetColorAlphaPremultiplied(Vector4) = 0;
			virtual void DrawImage(stmp::optional<IImage&>, const Vector2& outTopLeft) = 0;
			virtual void DrawImage(stmp::optional<IImage&>, const AABB2& outRect) = 0;
			virtual void DrawImage(stmp::optional<IImage&>, const Vector2& outTopLeft,
			                       const AABB2& inRect) = 0;
			virtual void DrawImage(stmp::optional<IImage&>, const AABB2& outRect,
			                       const AABB2& inRect) = 0;
			virtual void DrawImage(stmp::optional<IImage&>, const Vector2& outTopLeft,
			                       const Vector2& outTopRight, const Vector2& outBottomLeft,
			                       const AABB2& inRect) = 0;

			virtual void UpdateFlatGameMap() = 0;
			virtual void DrawFlatGameMap(const AABB2& outRect, const AABB2& inRect) = 0;
			/**
			 * Draws the flat game map into a rotated (non axis-aligned) quad,
			 * used for the circular/rotating minimap mode. `outTopLeft`,
			 * `outTopRight` and `outBottomLeft` define the destination quad
			 * the same way as the equivalent `DrawImage` overload does.
			 */
			virtual void DrawFlatGameMap(const Vector2& outTopLeft, const Vector2& outTopRight,
			                             const Vector2& outBottomLeft, const AABB2& inRect) = 0;

			/**
			 * Restricts subsequent 2D drawing to a circular region on
			 * screen, until the matching `EndClippingCircle()` call. Used
			 * for the circular minimap so its corners show the game world
			 * behind it instead of a solid color.
			 *
			 * This isn't guaranteed to be supported by every renderer
			 * backend (e.g. it requires a stencil buffer); the default
			 * implementation is a no-op, meaning the circular clip is
			 * simply skipped on backends that don't override it. Calls
			 * cannot be nested.
			 */
			virtual void BeginClippingCircle(const Vector2& center, float radius) {}
			virtual void EndClippingCircle() {}

			/**
			 * Restricts subsequent 2D drawing to an axis-aligned rectangular
			 * region on screen, until the matching `EndClippingRect()` call.
			 * Used for minimap modes where the map content rotates within a
			 * fixed rectangular window (e.g. a rotating rectangular
			 * minimap), so the rotated content doesn't spill past the
			 * window's edges.
			 *
			 * Like `BeginClippingCircle()`, this isn't guaranteed to be
			 * supported by every renderer backend; the default
			 * implementation is a no-op. Calls cannot be nested, and cannot
			 * be mixed with `BeginClippingCircle()`.
			 */
			virtual void BeginClippingRect(const AABB2& outRect) {}
			virtual void EndClippingRect() {}

			/**
			 * Returns whether this renderer backend is the software
			 * renderer. Used to skip features not yet supported by
			 * SWRenderer (e.g. long sprites, circle/rect clipping).
			 */
			virtual bool IsRendererSW() const { return false; }

			/** Finalizes a frame. */
			virtual void FrameDone() = 0;
			/** displays a rendered image to the screen. */
			virtual void Flip() = 0;
			/** get a rendered image. */
			virtual Handle<Bitmap> ReadBitmap() = 0;

			virtual float ScreenWidth() = 0;
			virtual float ScreenHeight() = 0;

			/**
			 * 2D drawing helpers
			 */
			void DrawLine(const Vector2& p1, const Vector2& p2, float thickness = 1.0F) {
				const auto& normal = (p2 - p1).Normalize().Perpendicular() * (thickness * 0.5F);
				const Vector2 vt[3] = { p1 - normal, p1 + normal, p2 - normal };
				DrawImage(nullptr, vt[0], vt[1], vt[2], AABB2(0, 0, 1, 1));
			}
			void DrawFilledRect(float x0, float y0, float x1, float y1) {
				DrawImage(nullptr, AABB2(x0, y0, x1 - x0, y1 - y0));
			}
			void DrawFilledRectFadeSolid(float x0, float y0, float x1, float y1,
                            float fadeStart, float fadeEnd,
                            Vector4 color0, Vector4 color1,
                            bool horizontal = false) {
				if (horizontal) {
					if (fadeStart > x0 && color0.w > 0.0F) {
						SetColorAlphaPremultiplied(color0);
						DrawFilledRect(x0, y0, fadeStart, y1);
					}
					DrawFilledRectFade(fadeStart, y0, fadeEnd, y1, color0, color1, true);
					if (x1 > fadeEnd && color1.w > 0.0F) {
						SetColorAlphaPremultiplied(color1);
						DrawFilledRect(fadeEnd, y0, x1, y1);
					}
				} else {
					if (fadeStart > y0 && color0.w > 0.0F) {
						SetColorAlphaPremultiplied(color0);
						DrawFilledRect(x0, y0, x1, fadeStart);
					}
					DrawFilledRectFade(x0, fadeStart, x1, fadeEnd, color0, color1, false);
					if (y1 > fadeEnd && color1.w > 0.0F) {
						SetColorAlphaPremultiplied(color1);
						DrawFilledRect(x0, fadeEnd, x1, y1);
					}
				}
			}
			virtual void DrawFilledRectFade(float x0, float y0, float x1, float y1,
                                Vector4 color0, Vector4 color1,
                                bool horizontal = false) = 0;
			void DrawOutlinedRect(float x0, float y0, float x1, float y1, int thickness = 1) {
				DrawFilledRect(x0, y0, x1, y0 + thickness); // top
				DrawFilledRect(x0, y1 - thickness, x1, y1); // bottom
				DrawFilledRect(x0, y0 + thickness, x0 + thickness, y1 - thickness); // left
				DrawFilledRect(x1 - thickness, y0 + thickness, x1, y1 - thickness); // right
			}
			void DrawFilledCircle(const Vector2& pos, float radius) {
				const int segments = Clamp((int)radius, 16, 64);
				const float step = M_PI_F * 2.0F / (float)segments;
				auto prev = pos + MakeVector2(radius, 0.0F);
				for (int i = 1; i <= segments; i++) {
					const float a = (float)i * step;
					const auto cur = pos + MakeVector2(cosf(a), sinf(a)) * radius;
					DrawFilledTriangle(pos, prev, cur);
					prev = cur;
				}
			}
			void DrawOutlinedCircle(const Vector2& pos, float radius, float thickness = 1.0F) {
				const float inner = radius - (thickness * 0.5F);
				const float outer = radius + (thickness * 0.5F);
				const int segments = Clamp((int)radius, 16, 64);
				const float step = M_PI_F * 2.0F / (float)segments;
				auto d1 = MakeVector2(1.0F, 0.0F); // cos(0), sin(0)
				for (int i = 0; i < segments; i++) {
					const float a2 = (float)(i + 1) * step;
					const auto d2 = MakeVector2(cosf(a2), sinf(a2));
					DrawFilledTriangle(pos + d1 * inner, pos + d1 * outer, pos + d2 * inner);
					DrawFilledTriangle(pos + d1 * outer, pos + d2 * outer, pos + d2 * inner);
					d1 = d2;
				}
			}
			virtual void DrawFilledTriangle(const Vector2& v0, const Vector2& v1, const Vector2& v2) = 0;
		};
	} // namespace client
} // namespace spades
