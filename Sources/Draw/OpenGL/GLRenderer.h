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

#include <map>
#include <memory>

#include "GLCameraBlurFilter.h"
#include "GLDynamicLight.h"
#include "GLSettings.h"
#include <Client/IGameMapListener.h>
#include <Client/IRenderer.h>
#include <Client/SceneDefinition.h>
#include <Core/Math.h>

namespace spades {
	namespace draw {
		class IGLDevice;
		class GLShader;
		class GLProgram;
		class GLProgramManager;
		class GLImageManager;
		class GLMapRenderer;
		class GLModelManager;
		class GLImageRenderer;
		class GLFlatMapRenderer;
		class IGLSpriteRenderer;
		class GLLongSpriteRenderer;
		class GLGlareRenderer;
		class GLDynamicLightTable;
		class GLDynamicLightOcclusionMaps;
		class GLFramebufferManager;
		class GLMapShadowRenderer;
		class GLModelRenderer;
		class IGLShadowMapRenderer;
		class GLWaterRenderer;
		class GLAmbientShadowRenderer;
		class GLMapOccupancy;
		class GLRadiosityRenderer;
		class GLLensDustFilter;
		class GLSoftLitSpriteRenderer;
		class GLAutoExposureFilter;
		class GLTemporalAAFilter;
		class GLFogFilter2;
		class GLProfiler;

		class GLRenderer : public client::IRenderer, public client::IGameMapListener {
			friend class GLShadowShader;
			friend class IGLShadowMapRenderer;
			friend class GLRadiosityRenderer;
			friend class GLSoftLitSpriteRenderer;

			struct DebugLine {
				Vector3 v1, v2;
				Vector4 color;
			};

			Handle<IGLDevice> device;
			std::unique_ptr<GLFramebufferManager> fbManager;
			client::GameMap* map; // TODO: get rid of raw pointers
			GLSettings settings;

			int renderWidth;
			int renderHeight;

			std::unique_ptr<GLProfiler> profiler;

			bool inited;
			bool sceneUsedInThisFrame;

			client::SceneDefinition sceneDef;
			Plane3 frustrum[6];

			std::vector<DebugLine> debugLines;
			std::vector<GLDynamicLight> lights;

			GLProgramManager* programManager;
			GLImageManager* imageManager;
			GLModelManager* modelManager;

			// TODO: get rid of these abominations called raw pointers
			std::unique_ptr<IGLShadowMapRenderer> shadowMapRenderer;
			GLMapShadowRenderer* mapShadowRenderer;
			GLMapRenderer* mapRenderer;
			GLImageRenderer* imageRenderer;
			GLFlatMapRenderer* flatMapRenderer;
			GLModelRenderer* modelRenderer;
			IGLSpriteRenderer* spriteRenderer;
			GLLongSpriteRenderer* longSpriteRenderer;
			std::unique_ptr<GLGlareRenderer> glareRenderer;
			std::unique_ptr<GLWaterRenderer> waterRenderer;
			std::unique_ptr<GLMapOccupancy> mapOccupancy;
			std::unique_ptr<GLDynamicLightTable> dynamicLightTable;
			std::unique_ptr<GLDynamicLightOcclusionMaps> dynamicLightOcclusionMaps;
			GLAmbientShadowRenderer* ambientShadowRenderer;
			GLRadiosityRenderer* radiosityRenderer;

			GLCameraBlurFilter* cameraBlur;
			GLLensDustFilter* lensDustFilter;
			GLAutoExposureFilter* autoExposureFilter;
			std::unique_ptr<GLTemporalAAFilter> temporalAAFilter;
			std::unique_ptr<GLFogFilter2> fogFilter2;

			// used when r_ssao = 1. only valid while rendering objects
			IGLDevice::UInteger ssaoBufferTexture;

			// used when r_srgb = 1
			IGLDevice::UInteger lastColorBufferTexture;

			float fogDistance;
			Vector3 fogColor;

			// used for color correction
			Vector3 smoothedFogColor;

			Matrix4 projectionMatrix;
			Matrix4 viewMatrix;
			Matrix4 projectionViewMatrix;
			bool renderingMirror;

			Vector4 drawColorAlphaPremultiplied;
			bool legacyColorPremultiply;

			unsigned int lastTime;
			std::uint32_t frameNumber = 0;

			/** Tells this renderer apart from every other one, even one made later at
			 * the same address. */
			static std::uint64_t NextInstanceId();
			const std::uint64_t instanceId = NextInstanceId();

			/** Counts the dynamic light passes, so that what binds textures for one
			 * knows when a new one starts. */
			std::uint32_t dynamicLightPass = 0;

			bool duringSceneRendering;

			void BuildProjectionMatrix();
			void BuildView();
			void BuildFrustrum();

			void RenderDebugLines();

			void RenderObjects(bool mirror = false);
			void RenderGhosts();

			/** Draws every model flagged `xray` again where the world hides it, in the
			 * colour the model asked for. The world marks the stencil buffer as it is
			 * drawn, which is what tells this pass where "behind the world" is. */
			void RenderXRayPass();

			void EnsureInitialized();
			void EnsureSceneStarted();
			void EnsureSceneNotStarted();

			void UpdateRenderSize();

			void Prepare2DRendering(bool reset = false);

		protected:
			~GLRenderer();

		public:
			/**
			 * The front of the depth range the first-person view's models are drawn
			 * into (`ModelRenderParam::depthHack`), so that they stay in front of the
			 * world. The world only reaches it within a hair of the near plane.
			 */
			static constexpr float kFirstPersonDepthEnd = 0.1F;

			GLRenderer(Handle<IGLDevice> glDevice);

			void Init() override;
			void Shutdown() override;

			Handle<client::IImage> RegisterImage(const char* filename) override;
			Handle<client::IModel> RegisterModel(const char* filename) override;

			void ClearCache() override;

			Handle<client::IImage> CreateImage(Bitmap&) override;
			Handle<client::IModel> CreateModel(VoxelModel&) override;

			GLProgram* RegisterProgram(const std::string& name);
			GLShader* RegisterShader(const std::string& name);

			void SetGameMap(stmp::optional<client::GameMap&>) override;
			void SetFogColor(Vector3 v) override;
			void SetFogDistance(float f) override { fogDistance = f; }

			/** The factor the world's lighting but the sun's, the fog and the sky are
			 * drawn with, in `[0, 1]`. */
			float GetDaylight() { return sceneDef.daylight; }

			/** The Fog Colour as set: the colour of the fog and the sky in full daylight. */
			Vector3 GetFullDaylightFogColor() { return fogColor; }

			/** The colour the fog and the sky are drawn in: the Fog Colour times the
			 * daylight. */
			Vector3 GetFogColor() { return fogColor * GetDaylight(); }
			float GetFogDistance() { return fogDistance; }

			/** The colour solid geometry fades to with distance: none when a fog filter
			 * draws the fog over it afterwards. */
			Vector3 GetFogColorForSolidPass();

			/** `GetFogColorForSolidPass` in full daylight: the sky's light, which the
			 * lighting shaders scale by the daylight themselves. */
			Vector3 GetFullDaylightFogColorForSolidPass();

			/** The factor the sun's light is drawn with. At `0` the sun casts no light
			 * and no shadow. */
			float GetSunlight() { return sceneDef.sunlight; }

			void StartScene(const client::SceneDefinition&) override;

			void RenderModel(client::IModel&, const client::ModelRenderParam&) override;

			void AddLight(const client::DynamicLightParam& light) override;

			void AddDebugLine(Vector3 a, Vector3 b, Vector4 color) override;

			void AddSprite(client::IImage&, Vector3 center, float radius, float rotation) override;
			void AddLongSprite(client::IImage&, Vector3 p1, Vector3 p2, float radius) override;
			void AddGlare(client::IImage&, const client::GlareParam&) override;

			void EndScene() override;

			void MultiplyScreenColor(Vector3) override;

			void SetColor(Vector4) override;
			void SetColorAlphaPremultiplied(Vector4) override;

			void DrawImage(stmp::optional<client::IImage&>, const Vector2& outTopLeft) override;
			void DrawImage(stmp::optional<client::IImage&>, const AABB2& outRect) override;
			void DrawImage(stmp::optional<client::IImage&>, const Vector2& outTopLeft,
			               const AABB2& inRect) override;
			void DrawImage(stmp::optional<client::IImage&>, const AABB2& outRect,
			               const AABB2& inRect) override;
			void DrawImage(stmp::optional<client::IImage&>, const Vector2& outTopLeft,
			               const Vector2& outTopRight, const Vector2& outBottomLeft,
			               const AABB2& inRect) override;

			void DrawFilledTriangle(const Vector2& v0, const Vector2& v1, const Vector2& v2) override;
			void DrawFilledRectFade(float x0, float y0, float x1, float y1, Vector4 colorTop, Vector4 colorBottom, bool horizontal) override;

			void DrawFlatGameMap(const AABB2& outRect, const AABB2& inRect) override;
			void DrawFlatGameMap(const Vector2& outTopLeft, const Vector2& outTopRight,
			                     const Vector2& outBottomLeft, const AABB2& inRect) override;

			void BeginClippingCircle(const Vector2& center, float radius) override;
			void EndClippingCircle() override;

			void BeginClippingRect(const AABB2& outRect) override;
			void EndClippingRect() override;

			void FrameDone() override;
			void Flip() override;
			Handle<Bitmap> ReadBitmap() override;

			float ScreenWidth() override;
			float ScreenHeight() override;

			int GetRenderWidth() const { return renderWidth; }
			int GetRenderHeight() const { return renderHeight; }

			GLSettings& GetSettings() { return settings; }
			IGLDevice& GetGLDevice() { return *device; }
			GLProfiler& GetGLProfiler() { return *profiler; }
			GLFramebufferManager* GetFramebufferManager() { return fbManager.get(); }
			IGLShadowMapRenderer* GetShadowMapRenderer() { return shadowMapRenderer.get(); }
			GLAmbientShadowRenderer* GetAmbientShadowRenderer() { return ambientShadowRenderer; }
			GLMapShadowRenderer* GetMapShadowRenderer() { return mapShadowRenderer; }
			GLMapOccupancy* GetMapOccupancy() { return mapOccupancy.get(); }
			/** This frame's dynamic lights, as the lighting shaders look them up. Made
			 * for the first frame with dynamic lights on, as are the occlusion maps. */
			GLDynamicLightTable* GetDynamicLightTable() { return dynamicLightTable.get(); }
			/** Where the map stops this frame's spotlights. */
			GLDynamicLightOcclusionMaps* GetDynamicLightOcclusionMaps() {
				return dynamicLightOcclusionMaps.get();
			}
			GLRadiosityRenderer* GetRadiosityRenderer() { return radiosityRenderer; }
			GLModelRenderer* GetModelRenderer() { return modelRenderer; }

			const Matrix4& GetProjectionMatrix() const { return projectionMatrix; }
			const Matrix4& GetProjectionViewMatrix() const { return projectionViewMatrix; }
			const Matrix4& GetViewMatrix() const { return viewMatrix; }

			std::uint32_t GetFrameNumber() const { return frameNumber; }
			std::uint64_t GetInstanceId() const { return instanceId; }
			/** The dynamic light pass under way, or the last one. */
			std::uint32_t GetDynamicLightPass() const { return dynamicLightPass; }

			bool IsRenderingMirror() const { return renderingMirror; }

			void GameMapChanged(int x, int y, int z, client::GameMap*) override;

			const client::SceneDefinition& GetSceneDef() const { return sceneDef; }

			bool BoxFrustrumCull(const AABB3&);
			bool SphereFrustrumCull(const Vector3& center, float radius);
		};
	} // namespace draw
} // namespace spades