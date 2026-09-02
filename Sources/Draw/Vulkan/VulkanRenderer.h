/*
 Copyright (c) 2013 Fran6nd

 This file is part of ZeroSpades, a fork of OpenSpades.

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

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>

#include <Client/IGameMapListener.h>
#include <Client/IRenderer.h>
#include <Client/SceneDefinition.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanMapRenderer;
		class VulkanModelRenderer;
		class VulkanSpriteRenderer;
		class VulkanLongSpriteRenderer;
		class VulkanImageRenderer;
		class VulkanWaterRenderer;
		class VulkanFlatMapRenderer;
		class VulkanShadowMapRenderer;
		class VulkanMapShadowRenderer;
		class VulkanFramebufferManager;
		class VulkanProgramManager;
		class VulkanModelManager;
		class VulkanImageManager;
		class VulkanPipelineCache;
		class VulkanProgram;
		class VulkanShader;
		class VulkanImage;
		class VulkanTemporaryImagePool;
		class VulkanBuffer;
		class VulkanAutoExposureFilter;
		class VulkanBloomFilter;
		class VulkanFogFilter;
		class VulkanDepthOfFieldFilter;
		class VulkanFXAAFilter;
		class VulkanCameraBlurFilter;
		class VulkanResampleBicubicFilter;
		class VulkanCavityOutlineFilter;
		class VulkanDepthResolveFilter;
		class VulkanColorCorrectionFilter;
		class VulkanLensFlareFilter;
		class VulkanAmbientShadowRenderer;
		class VulkanRadiosityRenderer;

		class VulkanRenderer : public client::IRenderer, public client::IGameMapListener {
			struct DebugLine {
				Vector3 v1, v2;
				Vector4 color;
			};

			// Deferred deletion queue entry
			struct DeferredDeletion {
				Handle<VulkanBuffer> buffer;
				uint32_t frameIndex; // Frame when this buffer was marked for deletion
			};

			// Pending texture upload (staged, not yet submitted)
			struct PendingUpload {
				VkCommandBuffer commandBuffer;
				Handle<VulkanBuffer> stagingBuffer;
			};

			Handle<gui::SDLVulkanDevice> device;
			client::GameMap* map;
			bool inited;
			bool sceneUsedInThisFrame;

			client::SceneDefinition sceneDef;

			std::vector<DebugLine> debugLines;
			std::vector<client::DynamicLightParam> lights;

			// Vulkan rendering resources
			std::vector<VkCommandBuffer> commandBuffers;
			VkRenderPass renderPass;
			// Variant of renderPass for 2D-only frames (no scene blit): clears the
			// swapchain colour from an UNDEFINED initial layout instead of LOADing it
			// from COLOR_ATTACHMENT_OPTIMAL. Render-pass-compatible with renderPass,
			// so the same swapchainFramebuffers are reused.
			VkRenderPass renderPass2D = VK_NULL_HANDLE;
			std::vector<VkFramebuffer> swapchainFramebuffers;

			// Frame synchronization
			uint32_t currentImageIndex;
			uint32_t currentFrameSlot; // cycles 0..maxFramesInFlight-1, matches semaphore slots
			VkSemaphore imageAvailableSemaphore;
			VkSemaphore renderFinishedSemaphore;
			std::vector<VkFence> inFlightFences; // sized to maxFramesInFlight, not image count
			// One entry per swapchain image: the in-flight-slot fence of the frame
			// that last rendered to that image (VK_NULL_HANDLE if none). Because the
			// swapchain has more images than frame slots, and per-image resources
			// (command buffer, descriptor pools, vertex buffers) are reused by image
			// index, we must wait on the previous user of an image before recording
			// into it again — otherwise (notably under MAILBOX on AMD) we reset/
			// overwrite resources the GPU is still reading. Not a Handle/owned fence,
			// just a reference into inFlightFences.
			std::vector<VkFence> imagesInFlight;

			// Deferred deletion queue for buffers that may still be in use by GPU
			std::vector<DeferredDeletion> deferredDeletions;
			std::vector<PendingUpload> pendingUploads;

			float fogDistance;
			Vector3 fogColor;

			Matrix4 projectionMatrix;
			Matrix4 viewMatrix;
			Matrix4 projectionViewMatrix;

			Plane3 frustrum[6];

			Vector4 drawColorAlphaPremultiplied;
			bool legacyColorPremultiply;

			unsigned int lastTime;
			float lastDt{0.0f};
			std::uint32_t frameNumber = 0;
			uint32_t lastSwapchainGeneration{0};

			// Counts consecutive per-frame queue-submit failures so a persistent
			// failure surfaces as an error instead of spamming the log every frame.
			int consecutiveSubmitFailures{0};

			bool duringSceneRendering;
			bool renderingMirror;

			std::unique_ptr<VulkanMapRenderer> mapRenderer;
			std::unique_ptr<VulkanModelRenderer> modelRenderer;
			std::unique_ptr<VulkanSpriteRenderer> spriteRenderer;
			std::unique_ptr<VulkanLongSpriteRenderer> longSpriteRenderer;
			std::unique_ptr<VulkanImageRenderer> imageRenderer;
			std::unique_ptr<VulkanWaterRenderer> waterRenderer;
			std::unique_ptr<VulkanFlatMapRenderer> flatMapRenderer;
			std::unique_ptr<VulkanShadowMapRenderer> shadowMapRenderer;
			std::unique_ptr<VulkanMapShadowRenderer> mapShadowRenderer;
			std::unique_ptr<VulkanAmbientShadowRenderer> ambientShadowRenderer;
			std::unique_ptr<VulkanRadiosityRenderer> radiosityRenderer;
			std::unique_ptr<VulkanFramebufferManager> framebufferManager;
			Handle<VulkanProgramManager> programManager;
			Handle<VulkanModelManager> modelManager;
			std::unique_ptr<VulkanImageManager> imageManager;
			Handle<VulkanPipelineCache> pipelineCache;
			Handle<VulkanTemporaryImagePool> temporaryImagePool;
		std::unique_ptr<VulkanAutoExposureFilter> autoExposureFilter;
		std::unique_ptr<VulkanBloomFilter> bloomFilter;
		std::unique_ptr<VulkanFogFilter> fogFilter;
		std::unique_ptr<VulkanDepthOfFieldFilter> depthOfFieldFilter;
		std::unique_ptr<VulkanFXAAFilter> fxaaFilter;
		std::unique_ptr<VulkanCameraBlurFilter> cameraBlurFilter;
		std::unique_ptr<VulkanResampleBicubicFilter> resampleBicubicFilter;
		std::unique_ptr<VulkanCavityOutlineFilter> cavityOutlineFilter;
		std::unique_ptr<VulkanColorCorrectionFilter> colorCorrectionFilter;
		std::unique_ptr<VulkanLensFlareFilter> lensFlareFilter;
		// MSAA only: resolves the multisampled scene depth into the single-sample
		// R32F resolved-depth image that soft sprites, water and the depth-reading
		// post filters sample. Null when MSAA is off.
		std::unique_ptr<VulkanDepthResolveFilter> depthResolveFilter;

			Handle<VulkanImage> whiteImage; // 1x1 white image for solid color rendering

			// Shared spotlight-cookie descriptors for the dynamic-light passes.
			// Both the map and model dlight pipelines bind a set-0 combined image
			// sampler holding the spotlight projection texture (Gfx/Spotlight.jpg),
			// or the 1x1 white image for point/linear lights. Descriptor sets are
			// cached per cookie image (the cookie images are static for the app's
			// lifetime, so a set is created once and reused across frames).
			VkDescriptorSetLayout dlightCookieSetLayout;
			VkDescriptorPool dlightCookiePool;
			std::unordered_map<VulkanImage*, VkDescriptorSet> dlightCookieCache;
			void EnsureDlightCookieResources();
			void DestroyDlightCookieResources();

			// Sky gradient rendering
			VkPipeline skyPipeline;
			VkPipelineLayout skyPipelineLayout;
			Handle<VulkanBuffer> skyVertexBuffer;
			Handle<VulkanBuffer> skyIndexBuffer;

			// MultiplyScreenColor pipeline (fullscreen multiplicative tint)
			VkPipeline multiplyColorPipeline;
			VkPipelineLayout multiplyColorPipelineLayout;
			std::vector<Vector3> pendingMultiplyColors;

			int renderWidth;
			int renderHeight;

			void BuildProjectionMatrix();
			void BuildView();
			void BuildFrustrum();

			void EnsureInitialized();
			void EnsureSceneStarted();
			void EnsureSceneNotStarted();

			void InitializeVulkanResources();
			void CreateRenderPass();
			void CreateFramebuffers();
			void CreateCommandBuffers();
			void CleanupVulkanResources();
			void RecreateSwapchainDependencies();

			uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
			uint32_t FindMemoryTypeWithFallback(uint32_t typeFilter,
			                                    VkMemoryPropertyFlags preferred,
			                                    VkMemoryPropertyFlags fallback);

			void RecordCommandBuffer(uint32_t imageIndex);
			void Record2DCommandBuffer(uint32_t imageIndex);

			// Sky gradient rendering
			void CreateSkyPipeline();
			void DestroySkyPipeline();
			void RenderSky(VkCommandBuffer commandBuffer);

			// MultiplyScreenColor rendering
			void CreateMultiplyColorPipeline();
			void DestroyMultiplyColorPipeline();
			void RenderMultiplyColors(VkCommandBuffer commandBuffer);

			// Debug line rendering
			VkPipeline debugLinePipeline;
			VkPipelineLayout debugLinePipelineLayout;
			void CreateDebugLinePipeline();
			void DestroyDebugLinePipeline();
			void RenderDebugLines(VkCommandBuffer commandBuffer);

			// Deferred deletion queue management
			void ProcessDeferredDeletions();
			void FlushPendingUploads();

			// Handles the result of a per-frame queue submit. Logs the first
			// failure, and after several consecutive failures raises an error so
			// the user sees a dialog rather than an endless silent log loop.
			// Resets on the first success.
			void HandleSubmitResult(VkResult result, const char* where);

			// Waits for the previous frame that rendered to currentImageIndex (if any)
			// and records the current slot's fence as that image's in-flight fence.
			// Call right after acquiring, before re-recording per-image resources.
			void WaitForImageInFlight();

		protected:
			~VulkanRenderer();

		public:
			VulkanRenderer(Handle<gui::SDLVulkanDevice> device);

			bool BoxFrustrumCull(const AABB3& box);
			bool SphereFrustrumCull(const Vector3& center, float radius);

			// IRenderer implementation
			void Init() override;
			void Shutdown() override;

			Handle<client::IImage> RegisterImage(const char* filename) override;
			Handle<client::IModel> RegisterModel(const char* filename) override;

			Handle<client::IImage> CreateImage(Bitmap& bitmap) override;
			Handle<client::IModel> CreateModel(VoxelModel& model) override;

			void SetGameMap(stmp::optional<client::GameMap&>) override;

			void SetFogDistance(float) override;
			void SetFogColor(Vector3) override;

			Vector3 GetFogColor() { return fogColor; }
			Vector3 GetFogColorForSolidPass();
			float GetFogDistance() { return fogDistance; }

			const client::SceneDefinition& GetSceneDef() const { return sceneDef; }
			const std::vector<client::DynamicLightParam>& GetDynamicLights() const { return lights; }

			// Canonical sun direction (points TOWARD the sun), matching the lens
			// flare, water and lit shaders. Single source of truth so the shadow
			// projection is derived from the sun rather than a duplicated constant.
			Vector3 GetSunDirection() const { return MakeVector3(0.0F, -1.0F, -1.0F).Normalize(); }

			const Matrix4& GetProjectionViewMatrix() const { return projectionViewMatrix; }
			const Matrix4& GetViewMatrix() const { return viewMatrix; }
			const Matrix4& GetProjectionMatrix() const { return projectionMatrix; }
			VulkanImage* GetWhiteImage() { return whiteImage.GetPointerOrNull(); }

			// Spotlight-cookie descriptors shared by the dynamic-light passes.
			// GetDlightCookieDescriptorSet returns a set-0 combined image sampler
			// for the given cookie image (pass nullptr for point/linear lights to
			// get the 1x1 white fallback).
			VkDescriptorSetLayout GetDlightCookieSetLayout();
			VkDescriptorSet GetDlightCookieDescriptorSet(VulkanImage* cookieImage);
			bool IsRenderingMirror() const { return renderingMirror; }
		int GetRenderWidth() const { return renderWidth; }
		int GetRenderHeight() const { return renderHeight; }

			void StartScene(const client::SceneDefinition& def) override;

			void AddDebugLine(Vector3 a, Vector3 b, Vector4 color) override;

			void AddSprite(client::IImage&, Vector3 center, float radius, float rotation) override;
			void AddLongSprite(client::IImage&, Vector3 p1, Vector3 p2, float radius) override;

			void AddLight(const client::DynamicLightParam& light) override;

			void RenderModel(client::IModel&, const client::ModelRenderParam& param) override;

			void EndScene() override;

			void MultiplyScreenColor(Vector3) override;

			void SetColor(Vector4) override;
			void SetColorAlphaPremultiplied(Vector4) override;

			void DrawImage(stmp::optional<client::IImage&>, const Vector2& outTopLeft) override;
			void DrawImage(stmp::optional<client::IImage&>, const AABB2& outRect) override;
			void DrawImage(stmp::optional<client::IImage&>, const Vector2& outTopLeft, const AABB2& inRect) override;
			void DrawImage(stmp::optional<client::IImage&>, const AABB2& outRect, const AABB2& inRect) override;
			void DrawImage(stmp::optional<client::IImage&>, const Vector2& outTopLeft, const Vector2& outTopRight,
			               const Vector2& outBottomLeft, const AABB2& inRect) override;

			void DrawFilledTriangle(const Vector2& v0, const Vector2& v1, const Vector2& v2) override;
			void DrawFilledRectFade(float x0, float y0, float x1, float y1, Vector4 color0,
			                        Vector4 color1, bool horizontal) override;

			void UpdateFlatGameMap() override;
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

			// IGameMapListener implementation
			void GameMapChanged(int x, int y, int z, client::GameMap*) override;

			// Vulkan-specific methods
			VulkanMapRenderer* GetMapRenderer() { return mapRenderer.get(); }
			VulkanModelRenderer* GetModelRenderer() { return modelRenderer.get(); }
			VulkanSpriteRenderer* GetSpriteRenderer() { return spriteRenderer.get(); }
			VulkanWaterRenderer* GetWaterRenderer() { return waterRenderer.get(); }
			VulkanShadowMapRenderer* GetShadowMapRenderer() { return shadowMapRenderer.get(); }
			VulkanMapShadowRenderer* GetMapShadowRenderer() { return mapShadowRenderer.get(); }
			VulkanAmbientShadowRenderer* GetAmbientShadowRenderer() { return ambientShadowRenderer.get(); }
			VulkanRadiosityRenderer* GetRadiosityRenderer() { return radiosityRenderer.get(); }
			VulkanFramebufferManager* GetFramebufferManager() { return framebufferManager.get(); }
			VkPipelineCache GetPipelineCache() const;

		// Program registration API
		VulkanProgram* RegisterProgram(const std::string& name);
		VulkanShader* RegisterShader(const std::string& name);
			Handle<gui::SDLVulkanDevice> GetDevice() { return device; }
			VkRenderPass GetRenderPass() const { return renderPass; } // Swapchain render pass (for UI)
			VkRenderPass GetOffscreenRenderPass() const; // Offscreen render pass (for 3D)
			uint32_t GetCurrentFrameIndex() const { return currentFrameSlot; }
			// Monotonic frame counter (incremented each FrameDone). Lets per-frame
			// resources distinguish multiple uses within one frame from frame-to-frame
			// reuse of the same in-flight slot.
			std::uint32_t GetFrameNumber() const { return frameNumber; }

			// Queue a buffer for deferred deletion (will be deleted after GPU is done with it)
			void QueueBufferForDeletion(Handle<VulkanBuffer> buffer);

			// Temporary image pool for render target reuse
			VulkanTemporaryImagePool* GetTemporaryImagePool() { return temporaryImagePool.GetPointerOrNull(); }
		};

	} // namespace draw
} // namespace spades
