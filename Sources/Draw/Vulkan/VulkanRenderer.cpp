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

#include <algorithm>
#include "VulkanRenderer.h"
#include "VulkanSpirvCache.h"
#include "VulkanMapRenderer.h"
#include "VulkanModelRenderer.h"
#include "VulkanSpriteRenderer.h"
#include "VulkanLongSpriteRenderer.h"
#include "VulkanImageRenderer.h"
#include "VulkanWaterRenderer.h"
#include "VulkanFlatMapRenderer.h"
#include "VulkanShadowMapRenderer.h"
#include "VulkanMapShadowRenderer.h"
#include "VulkanFramebufferManager.h"
#include "VulkanImageWrapper.h"
#include "VulkanImageManager.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include "VulkanOptimizedVoxelModel.h"
#include "VulkanModelManager.h"
#include "VulkanProgramManager.h"
#include "VulkanPipelineCache.h"
#include "VulkanTemporaryImagePool.h"
#include "VulkanAutoExposureFilter.h"
#include "VulkanBloomFilter.h"
#include "VulkanFogFilter.h"
#include "VulkanDepthOfFieldFilter.h"
#include "VulkanFXAAFilter.h"
#include "VulkanCameraBlurFilter.h"
#include "VulkanResampleBicubicFilter.h"
#include "VulkanCavityOutlineFilter.h"
#include "VulkanDepthResolveFilter.h"
#include "VulkanColorCorrectionFilter.h"
#include "VulkanLensFlareFilter.h"
#include "VulkanAmbientShadowRenderer.h"
#include "VulkanRadiosityRenderer.h"
#include <Gui/SDLVulkanDevice.h>
#include <Client/GameMap.h>
#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/Settings.h>
#include <cstring>
#include <vector>

SPADES_SETTING(r_dlights);
SPADES_SETTING(r_hdr);
SPADES_SETTING(r_bloom);
SPADES_SETTING(r_fogShadow);
SPADES_SETTING(r_modelShadows);
SPADES_SETTING(r_radiosity);
SPADES_SETTING(r_depthOfField);
SPADES_SETTING(r_fxaa);
SPADES_SETTING(r_cameraBlur);
SPADES_SETTING(r_scaleFilter);
SPADES_SETTING(r_water);
SPADES_SETTING(r_softParticles);
SPADES_SETTING(r_outlines);
SPADES_SETTING(r_colorCorrection);
SPADES_SETTING(r_lensFlare);

namespace {
	// Sky pipeline push constants, shared between the pipeline layout (range size)
	// and the draw call so both derive from one sizeof(). std140-style vec3→16
	// padding via the trailing _pad floats.
	struct SkyPushConstants {
		float fogColor[3];      float _pad0;
		float viewAxisFront[3]; float _pad1;
		float viewAxisUp[3];    float _pad2;
		float viewAxisSide[3];  float _pad3;
		float fovX;
		float fovY;
	};
} // namespace

namespace spades {
	namespace draw {

		VulkanRenderer::VulkanRenderer(Handle<gui::SDLVulkanDevice> dev)
		: device(std::move(dev)),
		  map(nullptr),
		  inited(false),
		  sceneUsedInThisFrame(false),
		  renderPass(VK_NULL_HANDLE),
		  currentImageIndex(0),
		  currentFrameSlot(0),
		  imageAvailableSemaphore(VK_NULL_HANDLE),
		  renderFinishedSemaphore(VK_NULL_HANDLE),
		  fogDistance(128.0f),
		  fogColor(MakeVector3(0, 0, 0)),
		  drawColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1)),
		  legacyColorPremultiply(false),
		  lastTime(0),
		  frameNumber(0),
		  duringSceneRendering(false),
		  renderingMirror(false),
		  mapRenderer(nullptr),
		  modelRenderer(nullptr),
		  spriteRenderer(nullptr),
		  longSpriteRenderer(nullptr),
			imageRenderer(nullptr),
		waterRenderer(nullptr),
		flatMapRenderer(nullptr),
		shadowMapRenderer(nullptr),
		mapShadowRenderer(nullptr),
		framebufferManager(nullptr),
		programManager(nullptr),
		imageManager(nullptr),
		skyPipeline(VK_NULL_HANDLE),
		skyPipelineLayout(VK_NULL_HANDLE),
		multiplyColorPipeline(VK_NULL_HANDLE),
		multiplyColorPipelineLayout(VK_NULL_HANDLE),
		debugLinePipeline(VK_NULL_HANDLE),
		debugLinePipelineLayout(VK_NULL_HANDLE),
		dlightCookieSetLayout(VK_NULL_HANDLE),
		dlightCookiePool(VK_NULL_HANDLE) {
		renderWidth = device->ScreenWidth();
		renderHeight = device->ScreenHeight();


		// Initialize program manager, pipeline cache and model manager
		try {
			programManager = Handle<VulkanProgramManager>::New(device);
			pipelineCache = Handle<VulkanPipelineCache>::New(device);
			modelManager = Handle<VulkanModelManager>::New(*this);
			temporaryImagePool = Handle<VulkanTemporaryImagePool>::New(device);
			InitializeVulkanResources();  // Create semaphores for synchronization

			// Create framebuffer manager for offscreen rendering
			framebufferManager = stmp::make_unique<VulkanFramebufferManager>(device, renderWidth, renderHeight);

			CreateRenderPass();  // Must create render pass before framebuffers
			CreateFramebuffers();
				CreateCommandBuffers();
			CreateSkyPipeline();
			CreateMultiplyColorPipeline();
			CreateDebugLinePipeline();

				mapRenderer = nullptr;
				modelRenderer = stmp::make_unique<VulkanModelRenderer>(*this);
				spriteRenderer = stmp::make_unique<VulkanSpriteRenderer>(*this);
				longSpriteRenderer = stmp::make_unique<VulkanLongSpriteRenderer>(*this);
				imageRenderer = stmp::make_unique<VulkanImageRenderer>(*this);
			imageManager = stmp::make_unique<VulkanImageManager>(*this, device);
			waterRenderer = stmp::make_unique<VulkanWaterRenderer>(*this, map);

			// Create a 1x1 white image for solid color rendering
			{
				Handle<Bitmap> whiteBmp(new Bitmap(1, 1), false);
				uint32_t* pixel = reinterpret_cast<uint32_t*>(whiteBmp->GetPixels());
				*pixel = 0xFFFFFFFF; // White (RGBA)
				Handle<client::IImage> imgHandle = CreateImage(*whiteBmp);

				// Get VulkanImage from the IImage
				VulkanImageWrapper* wrapper = dynamic_cast<VulkanImageWrapper*>(imgHandle.GetPointerOrNull());
				if (wrapper) {
					whiteImage = Handle<VulkanImage>(wrapper->GetVulkanImage());
				} else {
					whiteImage = Handle<VulkanImage>(dynamic_cast<VulkanImage*>(std::move(imgHandle).Unmanage()));
				}
			}

			// Preload shaders
			VulkanMapRenderer::PreloadShaders(*this);
			VulkanOptimizedVoxelModel::PreloadShaders(*this);
			VulkanWaterRenderer::PreloadShaders(*this);

			// Post-process filters. fogFilter is created unconditionally —
			// the mapShadowRenderer it samples is created lazily on SetGameMap,
			// which runs AFTER initialization; the runtime gate in the
			// post-process chain still skips fogFilter->Filter() until
			// mapShadowRenderer / AO / radiosity are all live.
			autoExposureFilter = stmp::make_unique<VulkanAutoExposureFilter>(*this);
			bloomFilter = stmp::make_unique<VulkanBloomFilter>(*this);
			fogFilter = stmp::make_unique<VulkanFogFilter>(*this);
			depthOfFieldFilter = stmp::make_unique<VulkanDepthOfFieldFilter>(*this);
			fxaaFilter = stmp::make_unique<VulkanFXAAFilter>(*this);
			cameraBlurFilter = stmp::make_unique<VulkanCameraBlurFilter>(*this);
			resampleBicubicFilter = stmp::make_unique<VulkanResampleBicubicFilter>(*this);
			cavityOutlineFilter = stmp::make_unique<VulkanCavityOutlineFilter>(*this);
			colorCorrectionFilter = stmp::make_unique<VulkanColorCorrectionFilter>(*this);
			lensFlareFilter = stmp::make_unique<VulkanLensFlareFilter>(*this);
			if (framebufferManager->IsMSAA())
				depthResolveFilter = stmp::make_unique<VulkanDepthResolveFilter>(*this);

			inited = true;
			lastSwapchainGeneration = device->GetSwapchainGeneration();
			vkDeviceWaitIdle(device->GetDevice());
		} catch (const std::exception& ex) {
			CleanupVulkanResources();
			throw;
		}
	}

	void VulkanRenderer::Shutdown() {
			SPADES_MARK_FUNCTION();

			if (!inited) {
				return;
			}

			// Remove this renderer from the GameMap's listener list before cleanup
			SetGameMap(nullptr);

			VkDevice vkDevice = device->GetDevice();
			if (vkDevice != VK_NULL_HANDLE) {
				vkDeviceWaitIdle(vkDevice);
			}

			// Invalidate shared pipeline caches before cleaning up
			VulkanOptimizedVoxelModel::InvalidateSharedPipeline(device.GetPointerOrNull());

			imageRenderer.reset();
			spriteRenderer.reset();
			longSpriteRenderer.reset();
			modelRenderer.reset();
			imageManager.reset();
			mapRenderer.reset();
			flatMapRenderer.reset();
			waterRenderer.reset();
			shadowMapRenderer.reset();
			ambientShadowRenderer.reset();
			radiosityRenderer.reset();
			mapShadowRenderer.reset();
			lensFlareFilter.reset();
			colorCorrectionFilter.reset();
			cavityOutlineFilter.reset();
			depthResolveFilter.reset();
			resampleBicubicFilter.reset();
			cameraBlurFilter.reset();
			fxaaFilter.reset();
			depthOfFieldFilter.reset();
			fogFilter.reset();
			bloomFilter.reset();
			autoExposureFilter.reset();
			framebufferManager.reset();
			programManager = nullptr;

			if (temporaryImagePool) {
				temporaryImagePool->Clear();
			}
			temporaryImagePool = nullptr;

			CleanupVulkanResources();

			inited = false;
		}

	VulkanRenderer::~VulkanRenderer() {
		// Ensure resources are cleaned up
		if (inited) {
			Shutdown();
		}
	}

		void VulkanRenderer::InitializeVulkanResources() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Semaphores are obtained from SDLVulkanDevice per-frame via AcquireNextImage
			// Initialize to null - they will be set when acquiring swapchain images
			imageAvailableSemaphore = VK_NULL_HANDLE;
			renderFinishedSemaphore = VK_NULL_HANDLE;

			// Create fences for frame synchronization (one per frame-in-flight slot)
			uint32_t maxFrames = device->GetMaxFramesInFlight();
			inFlightFences.resize(maxFrames);

			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled so first frame doesn't wait

			for (size_t i = 0; i < maxFrames; i++) {
				VkResult result = vkCreateFence(vkDevice, &fenceInfo, nullptr, &inFlightFences[i]);
				if (result != VK_SUCCESS) {
					// Clean up previously created fences
					for (size_t j = 0; j < i; j++) {
						vkDestroyFence(vkDevice, inFlightFences[j], nullptr);
					}
					SPRaise("Failed to create fence %zu (error code: %d)", i, result);
				}
			}

			// Per-swapchain-image fence tracking (no fence is owned here).
			imagesInFlight.assign(device->GetSwapchainImageViews().size(), VK_NULL_HANDLE);
		}

		void VulkanRenderer::CreateRenderPass() {
			SPADES_MARK_FUNCTION();

			// Color attachment
			// Note: Using LOAD_OP_LOAD because we blit the offscreen framebuffer before UI rendering
			VkAttachmentDescription colorAttachment{};
			colorAttachment.format = device->GetSwapchainImageFormat();
			colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			// No depth attachment: this pass only composites 2D (multiply-colour
			// tints + UI), none of which test or write depth. A depth attachment
			// here would be shared by every swapchain framebuffer and thus by
			// concurrent in-flight frames (a cross-frame depth hazard), and would
			// force every pipeline in the pass to supply pDepthStencilState. The
			// 3D scene has its own depth in the framebuffer manager.
			VkAttachmentReference colorAttachmentRef{};
			colorAttachmentRef.attachment = 0;
			colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorAttachmentRef;

			// The UI pass LOADs the swapchain image, which the scene blit wrote at
			// the TRANSFER stage moments earlier. Without TRANSFER in the source
			// masks that write is neither ordered nor made available before the
			// loadOp reads it -- reported as a READ_AFTER_WRITE hazard against
			// the barrier that performed the layout transition.
			VkSubpassDependency dependency{};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			                          VK_PIPELINE_STAGE_TRANSFER_BIT;
			// Include source access for presentation to properly synchronize with previous frame
			dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			                           VK_ACCESS_TRANSFER_WRITE_BIT;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			                           VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

			VkAttachmentDescription attachments[] = {colorAttachment};
			VkRenderPassCreateInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = attachments;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 1;
			renderPassInfo.pDependencies = &dependency;

			VkResult result = vkCreateRenderPass(device->GetDevice(), &renderPassInfo, nullptr, &renderPass);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create render pass (error code: %d)", result);
			}

			// 2D-only variant: no scene blit happens before it, so the swapchain
			// image is still UNDEFINED (first use) or PRESENT_SRC_KHR (after a prior
			// present) rather than COLOR_ATTACHMENT_OPTIMAL. LOADing it would be a
			// layout mismatch, so clear the colour from an UNDEFINED initial layout.
			VkAttachmentDescription attachments2D[] = {colorAttachment};
			attachments2D[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments2D[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			VkRenderPassCreateInfo renderPassInfo2D = renderPassInfo;
			renderPassInfo2D.pAttachments = attachments2D;

			result = vkCreateRenderPass(device->GetDevice(), &renderPassInfo2D, nullptr, &renderPass2D);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create 2D render pass (error code: %d)", result);
			}
		}

		uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
			VkPhysicalDeviceMemoryProperties memProperties;
			vkGetPhysicalDeviceMemoryProperties(device->GetPhysicalDevice(), &memProperties);

			for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
				if ((typeFilter & (1 << i)) &&
				    (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
					return i;
				}
			}

			SPRaise("Failed to find suitable memory type");
		}

		uint32_t VulkanRenderer::FindMemoryTypeWithFallback(uint32_t typeFilter,
		                                                   VkMemoryPropertyFlags preferred,
		                                                   VkMemoryPropertyFlags fallback) {
			VkPhysicalDeviceMemoryProperties memProperties;
			vkGetPhysicalDeviceMemoryProperties(device->GetPhysicalDevice(), &memProperties);

			// Try preferred first
			for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
				if ((typeFilter & (1 << i)) &&
				    (memProperties.memoryTypes[i].propertyFlags & preferred) == preferred) {
					return i;
				}
			}

			// Fall back to less strict requirements
			for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
				if ((typeFilter & (1 << i)) &&
				    (memProperties.memoryTypes[i].propertyFlags & fallback) == fallback) {
					return i;
				}
			}

			SPRaise("Failed to find suitable memory type");
		}

		void VulkanRenderer::CreateFramebuffers() {
			SPADES_MARK_FUNCTION();

			const auto& swapchainImageViews = device->GetSwapchainImageViews();
			swapchainFramebuffers.resize(swapchainImageViews.size());

			VkExtent2D swapchainExtent = device->GetSwapchainExtent();

			for (size_t i = 0; i < swapchainImageViews.size(); i++) {
				VkImageView attachments[] = {
					swapchainImageViews[i]
				};

				VkFramebufferCreateInfo framebufferInfo{};
				framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				framebufferInfo.renderPass = renderPass;
				framebufferInfo.attachmentCount = 1;
				framebufferInfo.pAttachments = attachments;
				framebufferInfo.width = swapchainExtent.width;
				framebufferInfo.height = swapchainExtent.height;
				framebufferInfo.layers = 1;

				VkResult result = vkCreateFramebuffer(device->GetDevice(), &framebufferInfo,
				                                      nullptr, &swapchainFramebuffers[i]);
				if (result != VK_SUCCESS) {
					SPRaise("Failed to create framebuffer (error code: %d)", result);
				}
			}

		}

		void VulkanRenderer::CreateCommandBuffers() {
			SPADES_MARK_FUNCTION();

			const auto& swapchainImageViews = device->GetSwapchainImageViews();
			commandBuffers.resize(swapchainImageViews.size());

			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool = device->GetCommandPool();
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

			VkResult result = vkAllocateCommandBuffers(device->GetDevice(), &allocInfo, commandBuffers.data());
			if (result != VK_SUCCESS) {
				SPRaise("Failed to allocate command buffers (error code: %d)", result);
			}

		}

		void VulkanRenderer::CleanupVulkanResources() {
			VkDevice vkDevice = device->GetDevice();

			if (!commandBuffers.empty() && vkDevice != VK_NULL_HANDLE) {
				vkFreeCommandBuffers(vkDevice, device->GetCommandPool(),
					static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
				commandBuffers.clear();
			}

			for (auto framebuffer : swapchainFramebuffers) {
				if (framebuffer != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
					vkDestroyFramebuffer(vkDevice, framebuffer, nullptr);
				}
			}
			swapchainFramebuffers.clear();

			DestroyDlightCookieResources();

			DestroySkyPipeline();
		DestroyMultiplyColorPipeline();
		DestroyDebugLinePipeline();

			if (renderPass != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyRenderPass(vkDevice, renderPass, nullptr);
				renderPass = VK_NULL_HANDLE;
			}
			if (renderPass2D != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyRenderPass(vkDevice, renderPass2D, nullptr);
				renderPass2D = VK_NULL_HANDLE;
			}

			// Semaphores are owned by SDLVulkanDevice, not destroyed here
			imageAvailableSemaphore = VK_NULL_HANDLE;
			renderFinishedSemaphore = VK_NULL_HANDLE;

			// Destroy fences
			for (VkFence fence : inFlightFences) {
				if (fence != VK_NULL_HANDLE) {
					vkDestroyFence(vkDevice, fence, nullptr);
				}
			}
			inFlightFences.clear();
		}

		void VulkanRenderer::RecreateSwapchainDependencies() {
		VkDevice vkDevice = device->GetDevice();

		// The device is already idle (SDLVulkanDevice::RecreateSwapchain called vkDeviceWaitIdle),
		// but wait again in case the renderer submitted work between that call and this one.
		vkDeviceWaitIdle(vkDevice);

		// Free command buffers before destroying the framebuffers they reference.
		if (!commandBuffers.empty()) {
			vkFreeCommandBuffers(vkDevice, device->GetCommandPool(),
				static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
			commandBuffers.clear();
		}

		// Destroy swapchain framebuffers (reference stale image views).
		for (auto fb : swapchainFramebuffers) {
			if (fb != VK_NULL_HANDLE)
				vkDestroyFramebuffer(vkDevice, fb, nullptr);
		}
		swapchainFramebuffers.clear();

		// Pull new dimensions from the swapchain.
		renderWidth = device->ScreenWidth();
		renderHeight = device->ScreenHeight();

		// Rebuild swapchain-dependent renderer resources.
		CreateFramebuffers();
		CreateCommandBuffers();

		// Rebuild the offscreen framebuffer manager, which is sized to render dimensions.
		// The water renderer holds references into the framebuffer manager, so reset it first.
		waterRenderer.reset();
		framebufferManager = stmp::make_unique<VulkanFramebufferManager>(device, renderWidth, renderHeight);
		waterRenderer = stmp::make_unique<VulkanWaterRenderer>(*this, map);

		// Device was waited idle above, so no frame references any image. Re-size to
		// the (possibly new) image count and clear stale per-image fence associations.
		imagesInFlight.assign(device->GetSwapchainImageViews().size(), VK_NULL_HANDLE);

		lastSwapchainGeneration = device->GetSwapchainGeneration();
		SPLog("Swapchain dependencies recreated (%dx%d)", renderWidth, renderHeight);
	}

	void VulkanRenderer::BuildProjectionMatrix() {
			SPADES_MARK_FUNCTION();
			projectionMatrix = sceneDef.ToVulkanProjectionMatrix();
		}

		void VulkanRenderer::BuildView() {
			SPADES_MARK_FUNCTION();
			viewMatrix = sceneDef.ToViewMatrix();
			projectionViewMatrix = projectionMatrix * viewMatrix;
		}

		void VulkanRenderer::BuildFrustrum() {
			// near/far planes
			frustrum[0] = Plane3::PlaneWithPointOnPlane(sceneDef.viewOrigin, sceneDef.viewAxis[2]);
			frustrum[1] = frustrum[0].Flipped();
			frustrum[0].w -= sceneDef.zNear;
			frustrum[1].w += sceneDef.zFar;

			// side planes
			float cx = cosf(sceneDef.fovX * 0.5F);
			float sx = sinf(sceneDef.fovX * 0.5F);
			float cy = cosf(sceneDef.fovY * 0.5F);
			float sy = sinf(sceneDef.fovY * 0.5F);

			frustrum[2] = Plane3::PlaneWithPointOnPlane(
			  sceneDef.viewOrigin, sceneDef.viewAxis[2] * sx - sceneDef.viewAxis[0] * cx);
			frustrum[3] = Plane3::PlaneWithPointOnPlane(
			  sceneDef.viewOrigin, sceneDef.viewAxis[2] * sx + sceneDef.viewAxis[0] * cx);
			frustrum[4] = Plane3::PlaneWithPointOnPlane(
			  sceneDef.viewOrigin, sceneDef.viewAxis[2] * sy - sceneDef.viewAxis[1] * cy);
			frustrum[5] = Plane3::PlaneWithPointOnPlane(
			  sceneDef.viewOrigin, sceneDef.viewAxis[2] * sy + sceneDef.viewAxis[1] * cy);
		}

		bool VulkanRenderer::BoxFrustrumCull(const AABB3& box) {
			if (renderingMirror) {
				AABB3 bx = box;
				std::swap(bx.min.z, bx.max.z);
				bx.min.z = 63.0F * 2.0F - bx.min.z;
				bx.max.z = 63.0F * 2.0F - bx.max.z;
				return PlaneCullTest(frustrum[0], bx) && PlaneCullTest(frustrum[1], bx) &&
				       PlaneCullTest(frustrum[2], bx) && PlaneCullTest(frustrum[3], bx) &&
				       PlaneCullTest(frustrum[4], bx) && PlaneCullTest(frustrum[5], bx);
			}
			return PlaneCullTest(frustrum[0], box) && PlaneCullTest(frustrum[1], box) &&
			       PlaneCullTest(frustrum[2], box) && PlaneCullTest(frustrum[3], box) &&
			       PlaneCullTest(frustrum[4], box) && PlaneCullTest(frustrum[5], box);
		}

		bool VulkanRenderer::SphereFrustrumCull(const Vector3& center, float radius) {
			if (renderingMirror) {
				Vector3 vx = center;
				vx.z = 63.0F * 2.0F - vx.z;
				for (int i = 0; i < 6; i++) {
					if (frustrum[i].GetDistanceTo(vx) < -radius)
						return false;
				}
				return true;
			}
			for (int i = 0; i < 6; i++) {
				if (frustrum[i].GetDistanceTo(center) < -radius)
					return false;
			}
			return true;
		}

				void VulkanRenderer::EnsureInitialized() {
			if (!inited) {
				SPRaise("Renderer not initialized");
			}
		}

		void VulkanRenderer::EnsureSceneStarted() {
			if (!duringSceneRendering) {
				SPRaise("Not in scene rendering");
			}
		}

		void VulkanRenderer::EnsureSceneNotStarted() {
			if (duringSceneRendering) {
				SPRaise("Already in scene rendering");
			}
		}

	VulkanProgram* VulkanRenderer::RegisterProgram(const std::string& name) {
		if (!programManager)
			return nullptr;
		return programManager->RegisterProgram(name);
	}

	VulkanShader* VulkanRenderer::RegisterShader(const std::string& name) {
		if (!programManager)
			return nullptr;
		return programManager->RegisterShader(name);
	}

		Handle<client::IImage> VulkanRenderer::RegisterImage(const char* filename) {
			SPADES_MARK_FUNCTION();

			if (!imageManager) {
				SPLog("RegisterImage: imageManager not initialized yet");
				return Handle<client::IImage>();
			}

			return imageManager->RegisterImage(filename);
		}

		void VulkanRenderer::EnsureDlightCookieResources() {
			if (dlightCookieSetLayout != VK_NULL_HANDLE)
				return;

			VkDevice vkDevice = device->GetDevice();

			VkDescriptorSetLayoutBinding binding{};
			binding.binding = 0;
			binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			binding.descriptorCount = 1;
			binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = 1;
			layoutInfo.pBindings = &binding;
			if (vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr,
			                                &dlightCookieSetLayout) != VK_SUCCESS) {
				SPLog("Warning: failed to create dlight cookie descriptor set layout");
				dlightCookieSetLayout = VK_NULL_HANDLE;
				return;
			}

			VkDescriptorPoolSize poolSize{};
			poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSize.descriptorCount = 16;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &poolSize;
			poolInfo.maxSets = 16;
			if (vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr,
			                           &dlightCookiePool) != VK_SUCCESS) {
				SPLog("Warning: failed to create dlight cookie descriptor pool");
				vkDestroyDescriptorSetLayout(vkDevice, dlightCookieSetLayout, nullptr);
				dlightCookieSetLayout = VK_NULL_HANDLE;
				dlightCookiePool = VK_NULL_HANDLE;
			}
		}

		void VulkanRenderer::DestroyDlightCookieResources() {
			VkDevice vkDevice = device->GetDevice();
			// Sets are freed implicitly when the pool is destroyed.
			dlightCookieCache.clear();
			if (dlightCookiePool != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyDescriptorPool(vkDevice, dlightCookiePool, nullptr);
				dlightCookiePool = VK_NULL_HANDLE;
			}
			if (dlightCookieSetLayout != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(vkDevice, dlightCookieSetLayout, nullptr);
				dlightCookieSetLayout = VK_NULL_HANDLE;
			}
		}

		VkDescriptorSetLayout VulkanRenderer::GetDlightCookieSetLayout() {
			EnsureDlightCookieResources();
			return dlightCookieSetLayout;
		}

		VkDescriptorSet VulkanRenderer::GetDlightCookieDescriptorSet(VulkanImage* cookieImage) {
			EnsureDlightCookieResources();
			if (dlightCookieSetLayout == VK_NULL_HANDLE || dlightCookiePool == VK_NULL_HANDLE)
				return VK_NULL_HANDLE;

			// Point/linear lights have no projection image — fall back to the 1x1
			// white texture so the sampler is valid (the shader ignores it for
			// non-spotlights anyway).
			if (!cookieImage)
				cookieImage = GetWhiteImage();
			if (!cookieImage)
				return VK_NULL_HANDLE;

			auto it = dlightCookieCache.find(cookieImage);
			if (it != dlightCookieCache.end())
				return it->second;

			VkDevice vkDevice = device->GetDevice();

			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = dlightCookiePool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &dlightCookieSetLayout;

			VkDescriptorSet set = VK_NULL_HANDLE;
			if (vkAllocateDescriptorSets(vkDevice, &allocInfo, &set) != VK_SUCCESS) {
				SPLog("Warning: failed to allocate dlight cookie descriptor set");
				return VK_NULL_HANDLE;
			}

			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = cookieImage->GetImageView();
			imageInfo.sampler = cookieImage->GetSampler();

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = set;
			write.dstBinding = 0;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.pImageInfo = &imageInfo;
			vkUpdateDescriptorSets(vkDevice, 1, &write, 0, nullptr);

			dlightCookieCache[cookieImage] = set;
			return set;
		}

		Handle<client::IModel> VulkanRenderer::RegisterModel(const char* filename) {
			SPADES_MARK_FUNCTION();
			return modelManager->RegisterModel(filename);
		}

	void VulkanRenderer::Init() {
		// Initialization is done in the constructor; make this a no-op if already initialized
		if (inited)
			return;
	}
	Handle<client::IImage> VulkanRenderer::CreateImage(Bitmap& bitmap) {
		SPADES_MARK_FUNCTION();
		try {
			uint32_t width = static_cast<uint32_t>(bitmap.GetWidth());
			uint32_t height = static_cast<uint32_t>(bitmap.GetHeight());

				// Create Vulkan image
				// Use RGBA format to match SDL's ABGR8888 bitmap format (which is RGBA in memory on little-endian)
				Handle<VulkanImage> vkImage(new VulkanImage(
					device, width, height, VK_FORMAT_R8G8B8A8_UNORM,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), false);

				// Upload bitmap data to the image
				// Flip the bitmap vertically for Vulkan (bitmap is in OpenGL bottom-left format)
				VkDeviceSize imageSize = width * height * 4;
				std::vector<uint8_t> flippedData(imageSize);
				const uint8_t* srcPixels = reinterpret_cast<const uint8_t*>(bitmap.GetPixels());
				uint32_t rowSize = width * 4;

				for (uint32_t y = 0; y < height; y++) {
					const uint8_t* srcRow = srcPixels + y * rowSize;
					uint8_t* dstRow = flippedData.data() + (height - 1 - y) * rowSize;
					std::memcpy(dstRow, srcRow, rowSize);
				}

				Handle<VulkanBuffer> stagingBuffer(new VulkanBuffer(
					device, imageSize,
					VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), false);

				stagingBuffer->UpdateData(flippedData.data(), imageSize);

				// Create temporary command buffer for upload
				VkCommandBufferAllocateInfo allocInfo{};
				allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				allocInfo.commandPool = device->GetCommandPool();
				allocInfo.commandBufferCount = 1;

				VkCommandBuffer commandBuffer;
				if (vkAllocateCommandBuffers(device->GetDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS) {
					SPRaise("Failed to allocate command buffer for screenshot");
				}

				VkCommandBufferBeginInfo beginInfo{};
				beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
					SPRaise("Failed to begin command buffer for screenshot");
				}

				// Transition image to transfer dst
				vkImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					0, VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

				// Copy buffer to image
				vkImage->CopyFromBuffer(commandBuffer, stagingBuffer->GetBuffer());

				// Transition image to shader read
				vkImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

				vkEndCommandBuffer(commandBuffer);

				// Submit immediately so any subsequent Update() calls via IImage::Update()
				// don't race against a deferred blank upload (which would overwrite glyph data)
				VkSubmitInfo uploadSubmit{};
				uploadSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				uploadSubmit.commandBufferCount = 1;
				uploadSubmit.pCommandBuffers = &commandBuffer;
				vkQueueSubmit(device->GetGraphicsQueue(), 1, &uploadSubmit, VK_NULL_HANDLE);
				vkQueueWaitIdle(device->GetGraphicsQueue());
				vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(), 1, &commandBuffer);

				// Create sampler for the image
				vkImage->CreateSampler();

				// Wrap in IImage interface
				return Handle<client::IImage>(
					new VulkanImageWrapper(vkImage, static_cast<float>(width), static_cast<float>(height)),
					false).Cast<client::IImage>();
			} catch (const std::exception& ex) {
				SPLog("Failed to create Vulkan image: %s", ex.what());
				return Handle<client::IImage>();
			}
		}

		Handle<client::IModel> VulkanRenderer::CreateModel(VoxelModel& model) {
			SPADES_MARK_FUNCTION();
			return Handle<VulkanOptimizedVoxelModel>::New(&model, *this).Cast<client::IModel>();
		}

		void VulkanRenderer::SetGameMap(stmp::optional<client::GameMap&> newMap) {
			SPADES_MARK_FUNCTION();

			client::GameMap* newMapPtr = newMap ? &*newMap : nullptr;

			if (map == newMapPtr) {
				return;
			}

			// The map-related renderers below own pipelines, buffers and images that
			// the GPU may still be reading from in-flight frames (up to
			// MAX_FRAMES_IN_FLIGHT). Tearing them down or recreating them while that
			// work is pending frees memory out from under the GPU — on AMD/amdvlk this
			// surfaces as a crash on the *next* map transition (leave game -> rejoin).
			// Drain all outstanding GPU work before touching these resources.
			if (device && device->GetDevice() != VK_NULL_HANDLE) {
				vkDeviceWaitIdle(device->GetDevice());
			}

			// Note: We intentionally don't call RemoveListener on the old map here.
			// When transitioning between clients (e.g., serverlist -> connect), the old map
			// may already be deleted, making the map pointer stale. The previous Client's
			// destructor already called SetGameMap(nullptr) which should have cleaned up properly.
			// If not, calling RemoveListener on a deleted map causes "mutex lock failed" errors.

			map = newMapPtr;

			if (map) {
				map->AddListener(this);
			}

			// Initialize map shadow renderer (heightmap shadow) BEFORE map renderer
			if (map) {
				mapShadowRenderer = stmp::make_unique<VulkanMapShadowRenderer>(*this, map);
				ambientShadowRenderer = stmp::make_unique<VulkanAmbientShadowRenderer>(*this, *map);
				radiosityRenderer = stmp::make_unique<VulkanRadiosityRenderer>(*this, *map);
			} else {
				mapShadowRenderer.reset();
				ambientShadowRenderer.reset();
				radiosityRenderer.reset();
			}

			// Initialize shadow map renderer first: the map/model sunlight pipeline
			// layouts reference its sampling descriptor-set layout (set 1).
			if (map) {
				shadowMapRenderer = stmp::make_unique<VulkanShadowMapRenderer>(*this);
			} else {
				shadowMapRenderer.reset();
			}

			// Initialize map renderer
			if (map) {
				mapRenderer = stmp::make_unique<VulkanMapRenderer>(map, *this);
				mapRenderer->CreatePipelines(framebufferManager->GetRenderPass()); // Use offscreen render pass for 3D rendering
				// Link shadow texture to map renderer
				if (mapShadowRenderer && ambientShadowRenderer && radiosityRenderer) {
					mapRenderer->UpdateShadowDescriptor(
						mapShadowRenderer->GetShadowImage(),
						ambientShadowRenderer->GetImageView(),
						ambientShadowRenderer->GetSampler(),
						radiosityRenderer->GetImageViewFlat(),
						radiosityRenderer->GetImageViewX(),
						radiosityRenderer->GetImageViewY(),
						radiosityRenderer->GetImageViewZ(),
						radiosityRenderer->GetSampler());
				}
			} else {
				mapRenderer.reset();
			}

			// Initialize flat map renderer (minimap)
			if (map) {
				flatMapRenderer = stmp::make_unique<VulkanFlatMapRenderer>(*this, *map);
			} else {
				flatMapRenderer.reset();
			}

			// Notify water renderer about map change
			if (waterRenderer) {
				waterRenderer->SetGameMap(map);
			}
		}

		void VulkanRenderer::SetFogDistance(float distance) {
			fogDistance = distance;
		}

		void VulkanRenderer::SetFogColor(Vector3 color) {
			fogColor = color;
		}

		Vector3 VulkanRenderer::GetFogColorForSolidPass() {
			if (r_fogShadow && shadowMapRenderer)
				return MakeVector3(0, 0, 0);
			else
				return fogColor;
		}

		void VulkanRenderer::StartScene(const client::SceneDefinition& def) {
			SPADES_MARK_FUNCTION();
			EnsureInitialized();
			EnsureSceneNotStarted();

			sceneDef = def;
			duringSceneRendering = true;
			sceneUsedInThisFrame = true;

			BuildProjectionMatrix();
			BuildView();
			BuildFrustrum();

			// Wait for the previous frame that used this semaphore slot to finish
			currentFrameSlot = device->GetCurrentFrame();
			vkWaitForFences(device->GetDevice(), 1, &inFlightFences[currentFrameSlot], VK_TRUE, UINT64_MAX);

			// Acquire next swapchain image
			currentImageIndex = device->AcquireNextImage(&imageAvailableSemaphore, &renderFinishedSemaphore);
			if (currentImageIndex == UINT32_MAX) {
				// Swapchain was recreated, try again
				currentImageIndex = device->AcquireNextImage(&imageAvailableSemaphore, &renderFinishedSemaphore);
			}
			// A second failure (e.g. repeated OUT_OF_DATE mid-resize, or a 0x0
			// minimised window) leaves no valid image. EndScene/Flip detect the
			// UINT32_MAX sentinel and drop the frame instead of indexing past the
			// command-buffer / framebuffer arrays.
			if (currentImageIndex == UINT32_MAX) {
				SPLog("[VulkanRenderer::StartScene] No swapchain image acquired; dropping frame");
			}

			// The swapchain has more images than frame slots, and per-image resources
			// (command buffer + the image renderer's descriptor pools / vertex buffers)
			// are reused by image index and reset when re-recording. Wait for the
			// previous frame that used THIS image to finish before EndScene records
			// into it, then mark this image as in flight on the current slot's fence.
			// Without this, MAILBOX (AMD) can reset descriptors the GPU is still
			// reading, dropping a HUD image for a frame.
			WaitForImageInFlight();
		}

		void VulkanRenderer::WaitForImageInFlight() {
			if (currentImageIndex >= imagesInFlight.size())
				return;
			if (imagesInFlight[currentImageIndex] != VK_NULL_HANDLE) {
				vkWaitForFences(device->GetDevice(), 1, &imagesInFlight[currentImageIndex],
				                VK_TRUE, UINT64_MAX);
			}
			imagesInFlight[currentImageIndex] = inFlightFences[currentFrameSlot];
		}

		void VulkanRenderer::AddDebugLine(Vector3 a, Vector3 b, Vector4 color) {
			SPADES_MARK_FUNCTION();
			EnsureInitialized();
			EnsureSceneStarted();
			DebugLine line;
			line.v1 = a;
			line.v2 = b;
			line.color = color;
			debugLines.push_back(line);
		}

		void VulkanRenderer::AddSprite(client::IImage& img, Vector3 center, float radius, float rotation) {
			SPADES_MARK_FUNCTION();
			EnsureInitialized();
			EnsureSceneStarted();

			VulkanImage* vkImage = nullptr;
			VulkanImageWrapper* wrapper = dynamic_cast<VulkanImageWrapper*>(&img);
			if (wrapper) {
				vkImage = wrapper->GetVulkanImage();
			} else {
				vkImage = dynamic_cast<VulkanImage*>(&img);
			}

			if (vkImage && spriteRenderer) {
				spriteRenderer->Add(vkImage, center, radius, rotation, drawColorAlphaPremultiplied);
			}
		}

		void VulkanRenderer::AddLongSprite(client::IImage& img, Vector3 p1, Vector3 p2, float radius) {
			SPADES_MARK_FUNCTION();
			EnsureInitialized();
			EnsureSceneStarted();

			VulkanImage* vkImage = nullptr;
			VulkanImageWrapper* wrapper = dynamic_cast<VulkanImageWrapper*>(&img);
			if (wrapper) {
				vkImage = wrapper->GetVulkanImage();
			} else {
				vkImage = dynamic_cast<VulkanImage*>(&img);
			}

			if (vkImage && longSpriteRenderer) {
				longSpriteRenderer->Add(vkImage, p1, p2, radius, drawColorAlphaPremultiplied);
			}
		}

		void VulkanRenderer::AddLight(const client::DynamicLightParam& light) {
			SPADES_MARK_FUNCTION();
			if (!r_dlights)
				return;
			if (!SphereFrustrumCull(light.origin, light.radius))
				return;
			EnsureInitialized();
			EnsureSceneStarted();
			lights.push_back(light);
		}

		void VulkanRenderer::RenderModel(client::IModel& model, const client::ModelRenderParam& param) {
			SPADES_MARK_FUNCTION();
			EnsureInitialized();
			EnsureSceneStarted();

			// Forward to model renderer
			if (modelRenderer) {
				VulkanModel* vkModel = dynamic_cast<VulkanModel*>(&model);
				if (vkModel) {
					modelRenderer->AddModel(vkModel, param);
				} else {
					SPLog("Warning: Model is not a VulkanModel, skipping");
				}
			}
		}

		void VulkanRenderer::EndScene() {
			SPADES_MARK_FUNCTION();
			EnsureSceneStarted();

			// StartScene failed to acquire a swapchain image: there is nothing valid
			// to record into. Drop the frame cleanly. The frame-slot fence was not
			// reset (so the next StartScene wait returns immediately), and clearing
			// sceneUsedInThisFrame routes Flip down its guarded 2D path rather than
			// presenting an invalid image index.
			if (currentImageIndex == UINT32_MAX || currentImageIndex >= commandBuffers.size()) {
				// Discard the scene content queued this frame; it is normally
				// consumed (and cleared) while recording the scene pass, which is
				// skipped here. Without this, sustained drops (a resize storm) grow
				// these lists unbounded.
				debugLines.clear();
				lights.clear();
				if (modelRenderer) modelRenderer->Clear();
				if (spriteRenderer) spriteRenderer->Clear();
				if (longSpriteRenderer) longSpriteRenderer->Clear();
				duringSceneRendering = false;
				sceneUsedInThisFrame = false;
				return;
			}

			if (sceneUsedInThisFrame) {
				// Calculate delta time for animations
				float dt = (float)(sceneDef.time - lastTime) / 1000.0f;
				if (dt > 0.1f) dt = 0.1f; // Cap dt to avoid large jumps
				if (dt < 0.0f) dt = 0.0f; // Handle timer wrap-around
				if (lastTime == 0) dt = 0.0f; // No animation on the first frame
				lastDt = dt;

				// Update water simulation
				if (waterRenderer) {
					waterRenderer->UpdateSimulation(dt);
				}
			}

			// Reset the fence for this frame slot (waited on in StartScene)
			vkResetFences(device->GetDevice(), 1, &inFlightFences[currentFrameSlot]);

			// Record command buffer for this frame
			RecordCommandBuffer(currentImageIndex);

			// Submit command buffer
			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

			VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
			// The acquired swapchain image is first written by a barrier + blit at
			// TRANSFER, which is EARLIER in the pipeline than
			// COLOR_ATTACHMENT_OUTPUT. Waiting only at the later stage lets that
			// write run before the acquire semaphore is signalled, which
			// validation reports as a WRITE_AFTER_READ hazard against
			// vkAcquireNextImageKHR. Wait at the transfer stage too.
			VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT |
			                                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores = waitSemaphores;
			submitInfo.pWaitDstStageMask = waitStages;

			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffers[currentImageIndex];

			VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = signalSemaphores;

			VkResult result = vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, inFlightFences[currentFrameSlot]);
			HandleSubmitResult(result, "scene");

			duringSceneRendering = false;
		}

		void VulkanRenderer::MultiplyScreenColor(Vector3 color) {
			SPADES_MARK_FUNCTION();
			EnsureSceneNotStarted();
			pendingMultiplyColors.push_back(color);
		}

		void VulkanRenderer::SetColor(Vector4 color) {
			drawColorAlphaPremultiplied = color;
			legacyColorPremultiply = true;
		}

		void VulkanRenderer::SetColorAlphaPremultiplied(Vector4 color) {
			legacyColorPremultiply = false;
			drawColorAlphaPremultiplied = color;
		}

		void VulkanRenderer::DrawImage(stmp::optional<client::IImage&> image, const Vector2& outTopLeft) {
			SPADES_MARK_FUNCTION();

			if (!image) {
				SPRaise("Null image provided to DrawImage");
				return;
			}

			DrawImage(image,
			          AABB2(outTopLeft.x, outTopLeft.y, image->GetWidth(), image->GetHeight()),
			          AABB2(0, 0, image->GetWidth(), image->GetHeight()));
		}

		void VulkanRenderer::DrawImage(stmp::optional<client::IImage&> image, const AABB2& outRect) {
			SPADES_MARK_FUNCTION();

			DrawImage(image, outRect,
			          AABB2(0, 0, image ? image->GetWidth() : 0, image ? image->GetHeight() : 0));
		}

		void VulkanRenderer::DrawImage(stmp::optional<client::IImage&> image, const Vector2& outTopLeft, const AABB2& inRect) {
			SPADES_MARK_FUNCTION();

			DrawImage(image,
			          AABB2(outTopLeft.x, outTopLeft.y, inRect.GetWidth(), inRect.GetHeight()),
			          inRect);
		}

		void VulkanRenderer::DrawImage(stmp::optional<client::IImage&> image, const AABB2& outRect, const AABB2& inRect) {
			SPADES_MARK_FUNCTION();

			DrawImage(image, Vector2::Make(outRect.GetMinX(), outRect.GetMinY()),
			          Vector2::Make(outRect.GetMaxX(), outRect.GetMinY()),
			          Vector2::Make(outRect.GetMinX(), outRect.GetMaxY()), inRect);
		}

		void VulkanRenderer::DrawImage(stmp::optional<client::IImage&> image, const Vector2& outTopLeft,
		                                const Vector2& outTopRight, const Vector2& outBottomLeft,
		                                const AABB2& inRect) {
			SPADES_MARK_FUNCTION();

			EnsureSceneNotStarted();

			// d = a + (b - a) + (c - a)
			//   = b + c - a
			Vector2 outBottomRight = outTopRight + outBottomLeft - outTopLeft;

			VulkanImage* img = nullptr;

			// Try to get VulkanImage from VulkanImageWrapper
			VulkanImageWrapper* wrapper = dynamic_cast<VulkanImageWrapper*>(image.get_pointer());
			if (wrapper) {
				img = wrapper->GetVulkanImage();
			} else {
				// Try direct cast to VulkanImage
				img = dynamic_cast<VulkanImage*>(image.get_pointer());
			}

			if (!img) {
				if (!image) {
					// Use white image for solid color rendering
					img = whiteImage.GetPointerOrNull();
					if (!img) {
						SPLog("DrawImage: Warning - white image not available");
						return;
					}
				} else {
					// invalid type: not VulkanImage or VulkanImageWrapper.
					SPLog("Warning: Unsupported image type in DrawImage, skipping");
					return;
				}
			}

			if (!imageRenderer) {
				// Image renderer not initialized yet
				SPLog("DrawImage: Skipping - imageRenderer not initialized");
				return;
			}

			imageRenderer->SetImage(img);

			Vector4 col = drawColorAlphaPremultiplied;
			if (legacyColorPremultiply) {
				// in legacy mode, image color is non alpha-premultiplied
				col.x *= col.w;
				col.y *= col.w;
				col.z *= col.w;
			}

			imageRenderer->Add(outTopLeft.x, outTopLeft.y, outTopRight.x, outTopRight.y,
			                   outBottomRight.x, outBottomRight.y, outBottomLeft.x, outBottomLeft.y,
			                   inRect.GetMinX(), inRect.GetMinY(), inRect.GetMaxX(),
			                   inRect.GetMinY(), inRect.GetMaxX(), inRect.GetMaxY(),
			                   inRect.GetMinX(), inRect.GetMaxY(), col.x, col.y, col.z, col.w);
		}

		void VulkanRenderer::DrawFilledTriangle(const Vector2& v0, const Vector2& v1,
		                                        const Vector2& v2) {
			SPADES_MARK_FUNCTION();

			EnsureSceneNotStarted();

			VulkanImage* img = whiteImage.GetPointerOrNull();
			if (!img || !imageRenderer)
				return;

			imageRenderer->SetImage(img);

			Vector4 col = drawColorAlphaPremultiplied;
			if (legacyColorPremultiply) {
				col.x *= col.w;
				col.y *= col.w;
				col.z *= col.w;
			}

			imageRenderer->AddTriangle(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y, col.x, col.y, col.z,
			                           col.w);
		}

		void VulkanRenderer::DrawFilledRectFade(float x0, float y0, float x1, float y1,
		                                        Vector4 color0, Vector4 color1, bool horizontal) {
			SPADES_MARK_FUNCTION();

			EnsureSceneNotStarted();

			if (color0.w <= 0.0F && color1.w <= 0.0F)
				return; // skip fully transparent draws

			VulkanImage* img = whiteImage.GetPointerOrNull();
			if (!img || !imageRenderer)
				return;

			imageRenderer->SetImage(img);

			// Unlike DrawFilledTriangle, the gradient colours arrive already
			// alpha-premultiplied from the caller, so legacyColorPremultiply does
			// not apply here — this matches GLRenderer.
			imageRenderer->AddGradient(x0, y0, x1, y0, x1, y1, x0, y1, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F,
			                           1.0F, 0.0F, 1.0F, color0.x, color0.y, color0.z, color0.w,
			                           color1.x, color1.y, color1.z, color1.w, horizontal);
		}

		void VulkanRenderer::UpdateFlatGameMap() {
			SPADES_MARK_FUNCTION();
			EnsureSceneNotStarted();
			if (flatMapRenderer)
				flatMapRenderer->UpdateChunks();
		}

		void VulkanRenderer::DrawFlatGameMap(const AABB2& outRect, const AABB2& inRect) {
			SPADES_MARK_FUNCTION();
			EnsureSceneNotStarted();
			if (flatMapRenderer)
				flatMapRenderer->Draw(outRect, inRect);
		}

		void VulkanRenderer::DrawFlatGameMap(const Vector2& outTopLeft, const Vector2& outTopRight,
		                                     const Vector2& outBottomLeft, const AABB2& inRect) {
			SPADES_MARK_FUNCTION();
			EnsureSceneNotStarted();
			if (flatMapRenderer)
				flatMapRenderer->Draw(outTopLeft, outTopRight, outBottomLeft, inRect);
		}

		// 2D clipping. Unlike GL, which stamps a stencil mask, this pass has no
		// depth/stencil attachment by design (see CreateRenderPass), so the
		// circle is a fragment-shader reject and the rect is a dynamic scissor.
		// Both are core Vulkan 1.0 with no optional features, and both travel
		// with the batch, since the image renderer records its draws later.
		void VulkanRenderer::BeginClippingCircle(const Vector2& center, float radius) {
			SPADES_MARK_FUNCTION();
			EnsureSceneNotStarted();
			if (imageRenderer)
				imageRenderer->SetClipCircle(center, radius);
		}

		void VulkanRenderer::EndClippingCircle() {
			SPADES_MARK_FUNCTION();
			EnsureSceneNotStarted();
			if (imageRenderer)
				imageRenderer->ClearClipCircle();
		}

		void VulkanRenderer::BeginClippingRect(const AABB2& outRect) {
			SPADES_MARK_FUNCTION();
			EnsureSceneNotStarted();
			if (imageRenderer)
				imageRenderer->SetClipRect(outRect);
		}

		void VulkanRenderer::EndClippingRect() {
			SPADES_MARK_FUNCTION();
			EnsureSceneNotStarted();
			if (imageRenderer)
				imageRenderer->ClearClipRect();
		}

		void VulkanRenderer::FrameDone() {
			SPADES_MARK_FUNCTION();

			if (!inited) {
				SPLog("[VulkanRenderer::FrameDone] Not initialized, skipping");
				return;
			}


			EnsureSceneNotStarted();

			// Flush image renderer like GLRenderer does
			// Note: We pass VK_NULL_HANDLE here since we'll record to command buffer later in Flip()
			// The Flush() call processes the batches but doesn't record to a command buffer yet
			if (imageRenderer) {
				// Store the batches for later recording in Flip()
				// Actually, we need to NOT flush here - just let batches accumulate
			}

			frameNumber++;
		}

		void VulkanRenderer::Flip() {
			SPADES_MARK_FUNCTION();

			if (!inited) {
				// Reachable only after Shutdown() (the constructor sets inited
				// before any Flip can run). Don't acquire/present here: acquiring
				// would signal an imageAvailable semaphore that nothing waits on
				// (a binary-semaphore-reuse violation on the next acquire), and the
				// freshly-acquired image isn't in PRESENT_SRC_KHR layout so it can't
				// legally be presented either. There is nothing valid to show, so
				// the correct action is to do nothing.
				return;
			}

		// Rebuild swapchain-dependent resources if the swapchain was recreated since last frame
		// (e.g. window resize triggered VK_ERROR_OUT_OF_DATE_KHR or VK_SUBOPTIMAL_KHR).
		if (device->GetSwapchainGeneration() != lastSwapchainGeneration) {
			RecreateSwapchainDependencies();
		}

			if (sceneUsedInThisFrame) {
				// Present the image (already rendered in EndScene)
				VkSemaphore waitSemaphores[] = {renderFinishedSemaphore};
						device->PresentImage(currentImageIndex, waitSemaphores, 1);
				
						lastTime = sceneDef.time;
						sceneUsedInThisFrame = false;			} else {
				// 2D-only rendering (like loading screen) - need to record and submit command buffer

				// Wait for the previous frame using this semaphore slot
				currentFrameSlot = device->GetCurrentFrame();
				vkWaitForFences(device->GetDevice(), 1, &inFlightFences[currentFrameSlot], VK_TRUE, UINT64_MAX);

				// Acquire next swapchain image
				currentImageIndex = device->AcquireNextImage(&imageAvailableSemaphore, &renderFinishedSemaphore);
				if (currentImageIndex == UINT32_MAX) {
					SPLog("[VulkanRenderer::Flip] Failed to acquire swapchain image");
					if (temporaryImagePool) { temporaryImagePool->ReleaseAll(); }
					return;
				}

				// Same per-image hazard guard as the scene path (see StartScene).
				WaitForImageInFlight();

				vkResetFences(device->GetDevice(), 1, &inFlightFences[currentFrameSlot]);

				// Record command buffer with the batched 2D images
				RecordCommandBuffer(currentImageIndex);

				// Submit command buffer
				VkSubmitInfo submitInfo{};
				submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

				VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
				// The acquired swapchain image is first written by a barrier + blit at
			// TRANSFER, which is EARLIER in the pipeline than
			// COLOR_ATTACHMENT_OUTPUT. Waiting only at the later stage lets that
			// write run before the acquire semaphore is signalled, which
			// validation reports as a WRITE_AFTER_READ hazard against
			// vkAcquireNextImageKHR. Wait at the transfer stage too.
			VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT |
			                                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
				submitInfo.waitSemaphoreCount = 1;
				submitInfo.pWaitSemaphores = waitSemaphores;
				submitInfo.pWaitDstStageMask = waitStages;

				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &commandBuffers[currentImageIndex];

				VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
				submitInfo.signalSemaphoreCount = 1;
				submitInfo.pSignalSemaphores = signalSemaphores;

				VkResult result = vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, inFlightFences[currentFrameSlot]);
				HandleSubmitResult(result, "2D");

				// Present the image with proper synchronization
				device->PresentImage(currentImageIndex, signalSemaphores, 1);
			}

			// Release all temporary images back to the pool for reuse next frame
			if (temporaryImagePool) {
				temporaryImagePool->ReleaseAll();
			}
		}

		Handle<Bitmap> VulkanRenderer::ReadBitmap() {
			SPADES_MARK_FUNCTION();

			if (!framebufferManager || !sceneUsedInThisFrame)
				return Handle<Bitmap>();

			// The final post-process result is mirrored into the resolved colour
			// image (== the raw colour image when MSAA is off), so read that.
			Handle<VulkanImage> srcImage = framebufferManager->GetResolvedColorImage();
			if (!srcImage)
				return Handle<Bitmap>();

			// Block until the frame submission is complete so the image is safe to read.
			vkWaitForFences(device->GetDevice(), 1, &inFlightFences[currentFrameSlot],
			                VK_TRUE, UINT64_MAX);

			VkFormat fmt = framebufferManager->GetMainColorFormat();

			// Bytes per texel for the offscreen color format.
			uint32_t bytesPerPixel;
			switch (fmt) {
				case VK_FORMAT_R16G16B16A16_SFLOAT: bytesPerPixel = 8; break;
				default:                             bytesPerPixel = 4; break;
			}

			VkDeviceSize imageSize = (VkDeviceSize)renderWidth * renderHeight * bytesPerPixel;

			// Create host-visible staging buffer for the copy destination.
			Handle<VulkanBuffer> stagingBuffer(
			    new VulkanBuffer(device, imageSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
			    false);

			// One-shot command buffer: transition → copy → transition back.
			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = device->GetCommandPool();
			allocInfo.commandBufferCount = 1;

			VkCommandBuffer cmd;
			vkAllocateCommandBuffers(device->GetDevice(), &allocInfo, &cmd);

			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(cmd, &beginInfo);

			// Offscreen image is in SHADER_READ_ONLY_OPTIMAL at end of frame.
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = srcImage->GetImage();
			barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

			barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
			                     1, &barrier);

			VkBufferImageCopy region{};
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.layerCount = 1;
			region.imageExtent = {(uint32_t)renderWidth, (uint32_t)renderHeight, 1};
			vkCmdCopyImageToBuffer(cmd, srcImage->GetImage(),
			                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			                       stagingBuffer->GetBuffer(), 1, &region);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
			                     nullptr, 1, &barrier);

			vkEndCommandBuffer(cmd);

			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &cmd;
			vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
			vkQueueWaitIdle(device->GetGraphicsQueue());
			vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(), 1, &cmd);

			// Map and convert to Bitmap (always RGBA8 output).
			const void* raw = stagingBuffer->Map();

			Handle<Bitmap> bmp(new Bitmap(renderWidth, renderHeight), false);
			uint32_t* pixels = bmp->GetPixels();
			const int count = renderWidth * renderHeight;

			// IEC 61966-2-1 sRGB transfer function. The offscreen colour
			// buffer holds linear values; the swapchain blit applies this
			// encoding on present, so the screenshot capture must replicate
			// it or the saved image (interpreted as sRGB by every viewer)
			// reads back darker than what the player sees on screen.
			auto linearToSRGBu8 = [](float c) -> uint8_t {
				if (c < 0.0f) c = 0.0f;
				if (c > 1.0f) c = 1.0f;
				float e = (c <= 0.0031308f) ? (12.92f * c)
				                            : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
				return static_cast<uint8_t>(e * 255.0f + 0.5f);
			};

			switch (fmt) {
				case VK_FORMAT_R8G8B8A8_SRGB:
					// The format itself already stores sRGB-encoded bytes —
					// blit-to-swapchain is identity, so a raw copy is correct.
					std::memcpy(pixels, raw, (size_t)count * 4);
					break;

				case VK_FORMAT_R8G8B8A8_UNORM: {
					// Linear bytes → sRGB-encoded bytes to match what the
					// swapchain blit shows on screen.
					const uint8_t* src = reinterpret_cast<const uint8_t*>(raw);
					for (int i = 0; i < count; i++) {
						uint8_t r = linearToSRGBu8(src[i * 4 + 0] / 255.0f);
						uint8_t g = linearToSRGBu8(src[i * 4 + 1] / 255.0f);
						uint8_t b = linearToSRGBu8(src[i * 4 + 2] / 255.0f);
						uint8_t a = src[i * 4 + 3];
						pixels[i] = r | (uint32_t(g) << 8) | (uint32_t(b) << 16) |
						            (uint32_t(a) << 24);
					}
					break;
				}

				case VK_FORMAT_R16G16B16A16_SFLOAT: {
					// HDR half-float → linear float → sRGB-encoded uint8.
					const uint16_t* src = reinterpret_cast<const uint16_t*>(raw);
					// Standard half→float via IEEE 754 bit manipulation (bias offset 127-15=112).
					auto halfToFloat = [](uint16_t h) -> float {
						uint32_t s = (h >> 15) & 1;
						uint32_t e = (h >> 10) & 0x1f;
						uint32_t m = h & 0x3ff;
						uint32_t bits;
						if (e == 0) {
							bits = (s << 31) | (m << 13); // ±0 or denorm (clamps to ~0)
						} else if (e == 31) {
							bits = (s << 31) | 0x7f800000u | (m << 13); // inf/NaN
						} else {
							bits = (s << 31) | ((e + 112) << 23) | (m << 13);
						}
						float f;
						std::memcpy(&f, &bits, 4);
						return f;
					};
					for (int i = 0; i < count; i++) {
						uint8_t r = linearToSRGBu8(halfToFloat(src[i * 4 + 0]));
						uint8_t g = linearToSRGBu8(halfToFloat(src[i * 4 + 1]));
						uint8_t b = linearToSRGBu8(halfToFloat(src[i * 4 + 2]));
						float aF = halfToFloat(src[i * 4 + 3]);
						if (aF < 0.0f) aF = 0.0f;
						if (aF > 1.0f) aF = 1.0f;
						uint8_t a = static_cast<uint8_t>(aF * 255.0f + 0.5f);
						pixels[i] = r | (uint32_t(g) << 8) | (uint32_t(b) << 16) |
						            (uint32_t(a) << 24);
					}
					break;
				}

				case VK_FORMAT_A2B10G10R10_UNORM_PACK32: {
					// Bits: [31:30]=A [29:20]=B [19:10]=G [9:0]=R; values
					// are linear UNORM, so encode to sRGB on the way out.
					const uint32_t* src = reinterpret_cast<const uint32_t*>(raw);
					for (int i = 0; i < count; i++) {
						uint32_t p = src[i];
						uint8_t r = linearToSRGBu8(((p >> 0) & 0x3ff) / 1023.0f);
						uint8_t g = linearToSRGBu8(((p >> 10) & 0x3ff) / 1023.0f);
						uint8_t b = linearToSRGBu8(((p >> 20) & 0x3ff) / 1023.0f);
						uint8_t a = static_cast<uint8_t>(((p >> 30) & 0x3) * 255 / 3);
						pixels[i] = r | (uint32_t(g) << 8) | (uint32_t(b) << 16) |
						            (uint32_t(a) << 24);
					}
					break;
				}

				default:
					// Unknown format: return solid black rather than garbage.
					std::memset(pixels, 0, (size_t)count * 4);
					break;
			}

			stagingBuffer->Unmap();

			// Flip vertically: the offscreen framebuffer is laid out with row 0
			// at the bottom of the displayed scene (viewport y is inverted at
			// draw time, but the readback returns raw memory order). PNG wants
			// row 0 at the top, so swap row pairs in place.
			for (int y = 0; y < renderHeight / 2; y++) {
				uint32_t* topRow = pixels + (size_t)y * renderWidth;
				uint32_t* bottomRow = pixels + (size_t)(renderHeight - 1 - y) * renderWidth;
				for (int x = 0; x < renderWidth; x++) {
					std::swap(topRow[x], bottomRow[x]);
				}
			}

			// Present the swapchain image acquired by StartScene. Without this
			// the screenshot path leaks an acquired-but-never-presented image
			// every call, exhausting the swapchain pool after a few shots and
			// stalling the next vkAcquireNextImageKHR until the GPU watchdog
			// kills the device.
			VkSemaphore waitSemaphores[] = {renderFinishedSemaphore};
			device->PresentImage(currentImageIndex, waitSemaphores, 1);
			lastTime = sceneDef.time;
			sceneUsedInThisFrame = false;

			return bmp;
		}

		float VulkanRenderer::ScreenWidth() {
			return static_cast<float>(renderWidth);
		}

		float VulkanRenderer::ScreenHeight() {
			return static_cast<float>(renderHeight);
		}

		void VulkanRenderer::GameMapChanged(int x, int y, int z, client::GameMap* map) {
			SPADES_MARK_FUNCTION();
			if (mapRenderer)
				mapRenderer->GameMapChanged(x, y, z, map);
			if (mapShadowRenderer)
				mapShadowRenderer->GameMapChanged(x, y, z, map);
			if (ambientShadowRenderer)
				ambientShadowRenderer->GameMapChanged(x, y, z, map);
			if (radiosityRenderer)
				radiosityRenderer->GameMapChanged(x, y, z, map);
			if (flatMapRenderer)
				flatMapRenderer->GameMapChanged(x, y, z, *map);
			if (waterRenderer)
				waterRenderer->GameMapChanged(x, y, z, map);
		}

		void VulkanRenderer::HandleSubmitResult(VkResult result, const char* where) {
			if (result == VK_SUCCESS) {
				consecutiveSubmitFailures = 0;
				return;
			}

			// A submit failure that repeats every frame means the renderer is wedged
			// (e.g. an unsupported operation recorded into the command buffer). Log
			// the first few, then raise so the user gets an error dialog instead of
			// an endless silent log loop.
			static const int kMaxConsecutiveSubmitFailures = 8;
			consecutiveSubmitFailures++;
			if (consecutiveSubmitFailures <= 3) {
				SPLog("Warning: Failed to submit %s command buffer (error code: %d)", where, result);
			}
			if (consecutiveSubmitFailures >= kMaxConsecutiveSubmitFailures) {
				SPRaise("Vulkan: %s command buffer submission failed %d frames in a row "
				        "(error code: %d). The current renderer settings may not be "
				        "supported on this GPU.",
				        where, consecutiveSubmitFailures, result);
			}
		}

		void VulkanRenderer::ProcessDeferredDeletions() {
			SPADES_MARK_FUNCTION();

			// Process deferred deletions for buffers that are no longer in use by the GPU
			// We keep buffers alive for MAX_FRAMES_IN_FLIGHT frames to ensure the GPU is done with them
			const uint32_t MAX_FRAMES_IN_FLIGHT = device->GetMaxFramesInFlight();

			auto it = deferredDeletions.begin();
			while (it != deferredDeletions.end()) {
				// Calculate how many frames have passed since this buffer was marked for deletion
				uint32_t framesPassed = frameNumber - it->frameIndex;

				// If enough frames have passed, the GPU is guaranteed to be done with this buffer
				if (framesPassed >= MAX_FRAMES_IN_FLIGHT) {
					// The buffer will be automatically destroyed when the Handle reference is released
					it = deferredDeletions.erase(it);
				} else {
					++it;
				}
			}
		}

		void VulkanRenderer::FlushPendingUploads() {
			if (pendingUploads.empty())
				return;

			// Collect all command buffers
			std::vector<VkCommandBuffer> cmdBufs;
			cmdBufs.reserve(pendingUploads.size());
			for (auto& u : pendingUploads)
				cmdBufs.push_back(u.commandBuffer);

			// Submit all uploads in a single batch
			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = (uint32_t)cmdBufs.size();
			submitInfo.pCommandBuffers = cmdBufs.data();

			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			VkFence fence;
			vkCreateFence(device->GetDevice(), &fenceInfo, nullptr, &fence);

			vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, fence);
			vkWaitForFences(device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
			vkDestroyFence(device->GetDevice(), fence, nullptr);

			vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(),
			                     (uint32_t)cmdBufs.size(), cmdBufs.data());

			// Queue staging buffers for deferred deletion (GPU is done, but keep
			// them alive for MAX_FRAMES_IN_FLIGHT frames to be safe with
			// descriptor sets that may still reference layout metadata).
			for (auto& u : pendingUploads)
				QueueBufferForDeletion(u.stagingBuffer);

			pendingUploads.clear();
		}

				VkRenderPass VulkanRenderer::GetOffscreenRenderPass() const {
		return framebufferManager ? framebufferManager->GetRenderPass() : renderPass;
	}

	VkPipelineCache VulkanRenderer::GetPipelineCache() const {
		return pipelineCache ? pipelineCache->GetCache() : VK_NULL_HANDLE;
	}

	void VulkanRenderer::QueueBufferForDeletion(Handle<VulkanBuffer> buffer) {
			if (buffer) {
				DeferredDeletion deletion;
				deletion.buffer = buffer;
				deletion.frameIndex = frameNumber;
				deferredDeletions.push_back(deletion);
			}
		}

		void VulkanRenderer::RecordCommandBuffer(uint32_t imageIndex) {
			SPADES_MARK_FUNCTION();

			// Flush any pending texture uploads before recording render commands
			FlushPendingUploads();

			// Process deferred deletions at the start of each frame
			ProcessDeferredDeletions();

			VkCommandBuffer commandBuffer = commandBuffers[imageIndex];

			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = 0;
			beginInfo.pInheritanceInfo = nullptr;

			VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
			if (result != VK_SUCCESS) {
				SPLog("Warning: Failed to begin recording command buffer");
				return;
			}

			if (waterRenderer) {
				waterRenderer->UploadWaveTextures(commandBuffer);
			}


		// Realize map chunks BEFORE starting render pass (updates buffers outside render pass)
		if (sceneUsedInThisFrame && mapRenderer) {
			mapRenderer->Realize();

			// Insert memory barrier to ensure host writes are visible to vertex shader reads
			// This synchronizes CPU-side buffer updates (from Map/Unmap) with GPU vertex fetching
			VkMemoryBarrier memoryBarrier{};
			memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			memoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			memoryBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;

			vkCmdPipelineBarrier(
				commandBuffer,
				VK_PIPELINE_STAGE_HOST_BIT,
				VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
				0,
				1, &memoryBarrier,
				0, nullptr,
				0, nullptr
			);
		}

		// Update map shadow heightmap texture (incremental updates from block changes)
		if (sceneUsedInThisFrame && mapShadowRenderer) {
			mapShadowRenderer->Update(commandBuffer);
		}

		// Upload any per-block ambient occlusion chunks that the background
		// dispatch finished computing this frame.
		if (sceneUsedInThisFrame && ambientShadowRenderer) {
			ambientShadowRenderer->Update(commandBuffer);
		}

		// Upload any radiosity (flat / X / Y / Z) chunks that finished
		// recomputing on the worker thread this frame.
		if (sceneUsedInThisFrame && radiosityRenderer) {
			radiosityRenderer->Update(commandBuffer);
		}

		// Render the cascaded shadow map BEFORE the main render pass. It is now a
		// models-only map driven by r_modelShadows alone — independent of r_fogShadow
		// (the fog in-scatter uses the separate 512² map shadow). So model shadows
		// work with fog off.
		if (sceneUsedInThisFrame && shadowMapRenderer && r_modelShadows) {
			shadowMapRenderer->Render(commandBuffer);
		} else if (shadowMapRenderer) {
			// Cascade not rendered this frame: keep the sampling UBO marked disabled
			// so the lit shaders skip (stale) model-shadow lookups.
			shadowMapRenderer->SetSamplingDisabled();
		}

		// Render mirror pass for water reflections (r_water >= 2)
		if (sceneUsedInThisFrame && framebufferManager && waterRenderer && (int)r_water >= 2) {
			renderingMirror = true;

			// Save original matrices
			Matrix4 originalViewMatrix = viewMatrix;
			Matrix4 originalProjectionViewMatrix = projectionViewMatrix;

			// Create reflected view matrix (reflect across water plane at z=63)
			Matrix4 reflectedView = viewMatrix * Matrix4::Translate(0, 0, 63);
			reflectedView = reflectedView * Matrix4::Scale(1, 1, -1);
			reflectedView = reflectedView * Matrix4::Translate(0, 0, -63);
			viewMatrix = reflectedView;
			projectionViewMatrix = projectionMatrix * viewMatrix;

			// Get fog color for background. With r_hdr the scene target is
			// R16G16B16A16_SFLOAT and therefore linear, so the fog colour has to
			// be linearized before it is cleared into it -- exactly what GL does
			// for its own mirror clear.
			Vector3 bgColor = GetFogColorForSolidPass();
			if ((int)r_hdr)
				bgColor *= bgColor; // linearize

			// Clear mirror framebuffer
			framebufferManager->ClearMirrorImage(commandBuffer, bgColor);

			// Begin mirror render pass
			VkRenderPassBeginInfo mirrorRenderPassInfo{};
			mirrorRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			mirrorRenderPassInfo.renderPass = framebufferManager->GetRenderPass();
			mirrorRenderPassInfo.framebuffer = framebufferManager->GetMirrorFramebuffer();
			mirrorRenderPassInfo.renderArea.offset = {0, 0};
			mirrorRenderPassInfo.renderArea.extent = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};

			VkClearValue mirrorClearValues[2];
			mirrorClearValues[0].color = {{bgColor.x, bgColor.y, bgColor.z, 1.0f}};
			mirrorClearValues[1].depthStencil = {1.0f, 0};
			mirrorRenderPassInfo.clearValueCount = 2;
			mirrorRenderPassInfo.pClearValues = mirrorClearValues;

			vkCmdBeginRenderPass(commandBuffer, &mirrorRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			// Set viewport with flipped Y for Vulkan
			VkViewport mirrorViewport{};
			mirrorViewport.x = 0.0f;
			mirrorViewport.y = static_cast<float>(renderHeight);
			mirrorViewport.width = static_cast<float>(renderWidth);
			mirrorViewport.height = -static_cast<float>(renderHeight);
			mirrorViewport.minDepth = 0.0f;
			mirrorViewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &mirrorViewport);

			VkRect2D mirrorScissor{};
			mirrorScissor.offset = {0, 0};
			mirrorScissor.extent = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};
			vkCmdSetScissor(commandBuffer, 0, 1, &mirrorScissor);

			// Note: Don't render sky in mirror pass - OpenGL just uses fog color clear
			// Rendering sky here with non-reflected view axes causes incorrect reflections

			// Render mirrored map
			if (mapRenderer) {
				mapRenderer->RenderSunlightPass(commandBuffer);
			}

			// Render mirrored models
			if (modelRenderer) {
				modelRenderer->RenderSunlightPass(commandBuffer, false);
			}

			vkCmdEndRenderPass(commandBuffer);

			// Restore original matrices
			viewMatrix = originalViewMatrix;
			projectionViewMatrix = originalProjectionViewMatrix;
			renderingMirror = false;

			// Transition mirror images to shader read for water sampling
			Handle<VulkanImage> mirrorColor = framebufferManager->GetMirrorColorImage();
			Handle<VulkanImage> mirrorDepth = framebufferManager->GetMirrorDepthImage();

			VkImageMemoryBarrier mirrorBarriers[2]{};
			mirrorBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			mirrorBarriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			mirrorBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			mirrorBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			mirrorBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			mirrorBarriers[0].image = mirrorColor->GetImage();
			mirrorBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			mirrorBarriers[0].subresourceRange.baseMipLevel = 0;
			mirrorBarriers[0].subresourceRange.levelCount = 1;
			mirrorBarriers[0].subresourceRange.baseArrayLayer = 0;
			mirrorBarriers[0].subresourceRange.layerCount = 1;
			mirrorBarriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			mirrorBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			mirrorBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			mirrorBarriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			mirrorBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			mirrorBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			mirrorBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			mirrorBarriers[1].image = mirrorDepth->GetImage();
			mirrorBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			mirrorBarriers[1].subresourceRange.baseMipLevel = 0;
			mirrorBarriers[1].subresourceRange.levelCount = 1;
			mirrorBarriers[1].subresourceRange.baseArrayLayer = 0;
			mirrorBarriers[1].subresourceRange.layerCount = 1;
			mirrorBarriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			mirrorBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 2, mirrorBarriers);

			// Under MSAA the mirror attachments are multisampled (for pipeline
			// compatibility with the scene), but the water shader samples reflections
			// as a regular sampler2D. Resolve colour (and depth at r_water >= 3) into
			// the single-sample images the water shader binds via GetWaterMirror*().
			if (framebufferManager->IsMSAA()) {
				framebufferManager->ResolveMirrorColor(commandBuffer,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				if ((int)r_water >= 3 && depthResolveFilter) {
					depthResolveFilter->Resolve(commandBuffer, mirrorDepth.GetPointerOrNull(),
						framebufferManager->GetMirrorDepthResolveImage().GetPointerOrNull());
				}
			}
		}

		// If we're rendering a 3D scene, render it to the offscreen framebuffer first
		if (sceneUsedInThisFrame && framebufferManager) {
			// Use fog color for solid pass (which may be black if fog shadow is
			// enabled). Linearized under r_hdr for the same reason as the mirror
			// clear above: the offscreen target is a linear float buffer.
			Vector3 bgColor = GetFogColorForSolidPass();
			if ((int)r_hdr)
				bgColor *= bgColor; // linearize

			// Begin offscreen render pass (to framebuffer manager's render target)
			VkRenderPassBeginInfo offscreenRenderPassInfo{};
			offscreenRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			offscreenRenderPassInfo.renderPass = framebufferManager->GetRenderPass();
			offscreenRenderPassInfo.framebuffer = framebufferManager->GetRenderFramebuffer();
			offscreenRenderPassInfo.renderArea.offset = {0, 0};
			offscreenRenderPassInfo.renderArea.extent = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};

			VkClearValue clearValues[2];
			clearValues[0].color = {{bgColor.x, bgColor.y, bgColor.z, 1.0f}};
			clearValues[1].depthStencil = {1.0f, 0};
			offscreenRenderPassInfo.clearValueCount = 2;
			offscreenRenderPassInfo.pClearValues = clearValues;

			vkCmdBeginRenderPass(commandBuffer, &offscreenRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			// Set viewport and scissor for 3D rendering
			// Vulkan has inverted Y-axis compared to OpenGL, so we need to flip the viewport
			VkViewport viewport{};
			viewport.x = 0.0f;
			viewport.y = static_cast<float>(renderHeight);
			viewport.width = static_cast<float>(renderWidth);
			viewport.height = -static_cast<float>(renderHeight);
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor{};
			scissor.offset = {0, 0};
			scissor.extent = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			// Render the 3D scene

			// Render sky gradient.
			//
			// GL never runs a flat sky pass when the volumetric fog filter is
			// active: the black FB clear leaves sky pixels at zero, then the
			// Fog post-process paints them entirely from `total * fogColor`,
			// which preserves the directional shadow shafts that occluders
			// cast through the in-scattered sunlight. Drawing a flat
			// fogColor sky here would double-tint sky pixels and erase that
			// shaft contrast.
			//
			// Sky.frag is kept as a workaround for the Vulkan Fog2 port,
			// which produces dimmer in-scatter than GL and leaves a near-
			// black sky on a black clear. We only suppress Sky.frag when
			// Fog1 is what actually runs (the variant selection mirrors
			// VulkanFogFilter's): r_fogShadow != 0 AND not (Fog2 active).
			const bool useFog2Pass = ((int)r_fogShadow == 2 && (int)r_radiosity != 0);
			const bool useFog1Pass = ((int)r_fogShadow != 0) && !useFog2Pass;
			if (!useFog1Pass)
				RenderSky(commandBuffer);

			// Render map
			if (!sceneDef.skipWorld && mapRenderer) {
				mapRenderer->RenderSunlightPass(commandBuffer);
			}

			// Render models
			if (modelRenderer) {
				modelRenderer->RenderSunlightPass(commandBuffer, false);
			}

			// Render dynamic lights (muzzle flash, flashlight, etc.)
			if (!lights.empty()) {
				std::vector<void*> lightPtrs;
				lightPtrs.reserve(lights.size());
				for (auto& l : lights) {
					lightPtrs.push_back(&l);
				}
				if (!sceneDef.skipWorld && mapRenderer) {
					mapRenderer->RenderDynamicLightPass(commandBuffer, lightPtrs);
				}
				if (modelRenderer) {
					modelRenderer->RenderDynamicLightPass(commandBuffer, lightPtrs);
				}
			}

			// Outlines are rendered later as a screen-space cavity post-process
			// pass (see PP-6 below). The per-renderer wireframe / inverted-hull
			// approaches were retired because they either flicker on MoltenVK
			// (line rasterisation), need unsupported features (geom shader), or
			// leave visible seams at voxel corners (inverted hull).

			// Ghost model pass: depth prepass then semi-transparent color pass
		if (modelRenderer) {
			modelRenderer->Prerender(commandBuffer, true);
			modelRenderer->RenderSunlightPass(commandBuffer, true);
		}

		// Soft particles sample the scene depth mid-frame. Under MSAA they read the
		// single-sample resolved depth (filled right after the opaque pass), so soft
		// particles work at every sample count.
		bool useSoftParticles = spriteRenderer && spriteRenderer->IsSoftParticles();

			// Long sprites (tracers + the weapon reflex reticle), non-soft particles
			// and debug lines are transparent and must be drawn AFTER the water pass,
			// otherwise the water surface paints over them wherever it sits in front
			// of their background (matches GL, which draws these transparent overlays
			// after water). Drawn here in the order particles -> long sprites -> debug.
			// When the water pass will run, defer them to it (same render pass, the
			// pipelines are render-pass-compatible); otherwise draw them here.
			const bool willRenderWater = (int)r_water > 0 && waterRenderer && framebufferManager &&
			                             framebufferManager->GetMirrorColorImage();

			if (!willRenderWater) {
				if (!useSoftParticles && spriteRenderer)
					spriteRenderer->Render(commandBuffer, imageIndex);
				if (longSpriteRenderer)
					longSpriteRenderer->Render(commandBuffer, imageIndex);
				RenderDebugLines(commandBuffer);
				debugLines.clear();
			}

			// Clear for next frame. lights survives until after the
			// post-process chain: the lens flare filter reads it for
			// r_lensFlareDynamic.
			if (modelRenderer) {
				modelRenderer->Clear();
			}

			// End offscreen render pass (scene without water is now complete)
			vkCmdEndRenderPass(commandBuffer);

			Handle<VulkanImage> offscreenColor = framebufferManager->GetColorImage();
			Handle<VulkanImage> offscreenDepth = framebufferManager->GetDepthImage();

			// Under MSAA the scene depth is multisampled and can't be sampled as a
			// regular sampler2D. It is final after the opaque pass (sprites are
			// colour-only, water doesn't write depth), so resolve it once here into
			// the single-sample R32F resolved-depth image and reuse it for everything
			// downstream: soft particles, water refraction/reflection depth, and the
			// depth-reading post filters (all via GetResolvedDepthImage()). The raw
			// multisampled depth is left in SHADER_READ_ONLY, so the per-branch depth
			// barriers below are skipped under MSAA.
			const bool msaaScene = framebufferManager->IsMSAA();
			if (msaaScene && depthResolveFilter) {
				VkImageMemoryBarrier dRead{};
				dRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				dRead.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				dRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				dRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				dRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				dRead.image = offscreenDepth->GetImage();
				dRead.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
				dRead.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				dRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &dRead);

				depthResolveFilter->Resolve(commandBuffer, offscreenDepth.GetPointerOrNull(),
					framebufferManager->GetResolvedDepthImage().GetPointerOrNull());
			}

			{
				// Transition both colour and depth to shader read-only so the water
				// pass and the depth-reading post filters can sample them. Soft
				// particles are NOT drawn here: they sample scene depth and so must be
				// drawn after the water pass (otherwise water occludes them), in a
				// color-only pass below — see the relocated soft-particle pass.
				VkImageMemoryBarrier barriers[2]{};
				barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barriers[0].image = offscreenColor->GetImage();
				barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barriers[0].subresourceRange.baseMipLevel = 0;
				barriers[0].subresourceRange.levelCount = 1;
				barriers[0].subresourceRange.baseArrayLayer = 0;
				barriers[0].subresourceRange.layerCount = 1;
				barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

				barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barriers[1].image = offscreenDepth->GetImage();
				barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				barriers[1].subresourceRange.baseMipLevel = 0;
				barriers[1].subresourceRange.levelCount = 1;
				barriers[1].subresourceRange.baseArrayLayer = 0;
				barriers[1].subresourceRange.layerCount = 1;
				barriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

				// Colour only: under MSAA the depth was already transitioned and
				// resolved above; at 1x it stays in ATTACHMENT layout (never sampled).
				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1u, barriers);
			}

			// Depth is final here (sprites are colour-only, water doesn't write depth).
			if (!msaaScene) {
				framebufferManager->CopySceneDepthForSampling(commandBuffer);
			}

			// Clear transparent buffers once they've been drawn. Non-soft particles
			// drawn in the offscreen pass (no water) are cleared here; soft particles
			// and the water-deferred buffers are cleared after they're drawn below.
			if (spriteRenderer && !useSoftParticles && !willRenderWater) {
				spriteRenderer->Clear();
			}
			if (longSpriteRenderer && !willRenderWater) {
				longSpriteRenderer->Clear();
			}

			// Water works under MSAA: the screen-copy / mirror paths resolve the
			// multisampled scene into single-sample images the water shader samples
			// (see CopySceneForWaterSampling / CopyToMirrorImage / ResolveMirrorColor
			// and the GetWater*() accessors), and refraction depth reuses the scene's
			// resolved depth.
			bool waterEnabled = (int)r_water > 0 && waterRenderer;

			// Copy scene to mirror images for water refraction when no real mirror pass
			if (waterEnabled && framebufferManager->GetMirrorColorImage() && (int)r_water < 2) {
				framebufferManager->CopyToMirrorImage(commandBuffer);
			}

			// Copy scene to screen copy images for water refraction sampling
			// (water renders to the same framebuffer, so it can't sample from it directly)
			if (waterEnabled) {
				framebufferManager->CopySceneForWaterSampling(commandBuffer);
			}

			if (waterEnabled && framebufferManager->GetMirrorColorImage()) {

				// Transition renderColorImage and renderDepthImage back to COLOR_ATTACHMENT_OPTIMAL
				// for water rendering
				VkImageMemoryBarrier backToAttachment[2]{};
				backToAttachment[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				backToAttachment[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				backToAttachment[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				backToAttachment[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				backToAttachment[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				backToAttachment[0].image = offscreenColor->GetImage();
				backToAttachment[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				backToAttachment[0].subresourceRange.baseMipLevel = 0;
				backToAttachment[0].subresourceRange.levelCount = 1;
				backToAttachment[0].subresourceRange.baseArrayLayer = 0;
				backToAttachment[0].subresourceRange.layerCount = 1;
				backToAttachment[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				backToAttachment[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

				backToAttachment[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				backToAttachment[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				backToAttachment[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				backToAttachment[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				backToAttachment[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				backToAttachment[1].image = offscreenDepth->GetImage();
				backToAttachment[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				backToAttachment[1].subresourceRange.baseMipLevel = 0;
				backToAttachment[1].subresourceRange.levelCount = 1;
				backToAttachment[1].subresourceRange.baseArrayLayer = 0;
				backToAttachment[1].subresourceRange.layerCount = 1;
				backToAttachment[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				backToAttachment[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
					0, 0, nullptr, 0, nullptr, msaaScene ? 2u : 1u, backToAttachment);

				// Begin water render pass with LOAD_OP to preserve scene content
				VkRenderPassBeginInfo waterRenderPassInfo{};
				waterRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				waterRenderPassInfo.renderPass = framebufferManager->GetWaterRenderPass();
				waterRenderPassInfo.framebuffer = framebufferManager->GetRenderFramebuffer();
				waterRenderPassInfo.renderArea.offset = {0, 0};
				waterRenderPassInfo.renderArea.extent = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};
				waterRenderPassInfo.clearValueCount = 0; // No clear, using LOAD_OP
				waterRenderPassInfo.pClearValues = nullptr;

				vkCmdBeginRenderPass(commandBuffer, &waterRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
				waterRenderer->RenderSunlightPass(commandBuffer);

				// Deferred transparent stage (see the offscreen pass): long sprites
				// (tracers + reflex reticle), non-soft particles and debug lines are
				// drawn here, over the finished water, so it can't occlude them. The
				// water render pass is render-pass-compatible with the offscreen one,
				// so these pipelines are valid as-is. Set viewport/scissor explicitly
				// (Y-flipped, as elsewhere) rather than relying on inherited state.
				{
					VkViewport tvp{};
					tvp.x = 0.0f;
					tvp.y = static_cast<float>(renderHeight);
					tvp.width = static_cast<float>(renderWidth);
					tvp.height = -static_cast<float>(renderHeight);
					tvp.minDepth = 0.0f;
					tvp.maxDepth = 1.0f;
					vkCmdSetViewport(commandBuffer, 0, 1, &tvp);
					VkRect2D tsc{};
					tsc.offset = {0, 0};
					tsc.extent = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};
					vkCmdSetScissor(commandBuffer, 0, 1, &tsc);

					if (!useSoftParticles && spriteRenderer)
						spriteRenderer->Render(commandBuffer, imageIndex);
					if (longSpriteRenderer)
						longSpriteRenderer->Render(commandBuffer, imageIndex);
					RenderDebugLines(commandBuffer);
					debugLines.clear();
				}

				vkCmdEndRenderPass(commandBuffer);

				// Clear the deferred transparent buffers now that they've been drawn.
				if (!useSoftParticles && spriteRenderer)
					spriteRenderer->Clear();
				if (longSpriteRenderer)
					longSpriteRenderer->Clear();

				// Transition color and depth back to SHADER_READ_ONLY for the post-process chain.
				VkImageMemoryBarrier waterPostBarriers[2]{};
				waterPostBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				waterPostBarriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				waterPostBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				waterPostBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				waterPostBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				waterPostBarriers[0].image = offscreenColor->GetImage();
				waterPostBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				waterPostBarriers[0].subresourceRange.baseMipLevel = 0;
				waterPostBarriers[0].subresourceRange.levelCount = 1;
				waterPostBarriers[0].subresourceRange.baseArrayLayer = 0;
				waterPostBarriers[0].subresourceRange.layerCount = 1;
				waterPostBarriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				waterPostBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

				waterPostBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				waterPostBarriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				waterPostBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				waterPostBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				waterPostBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				waterPostBarriers[1].image = offscreenDepth->GetImage();
				waterPostBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				waterPostBarriers[1].subresourceRange.baseMipLevel = 0;
				waterPostBarriers[1].subresourceRange.levelCount = 1;
				waterPostBarriers[1].subresourceRange.baseArrayLayer = 0;
				waterPostBarriers[1].subresourceRange.layerCount = 1;
				waterPostBarriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				waterPostBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, msaaScene ? 2u : 1u, waterPostBarriers);
			}

			// Soft particles (smoke/blood) are drawn here, after water, so the water
			// surface can't occlude them — same reason long sprites/non-soft particles
			// are deferred into the water pass above. They can't share the water pass:
			// they sample scene depth in the shader, which is bound as a depth
			// attachment there (a feedback loop). So they render in their own
			// color-only pass that samples the depth texture (GetResolvedDepthImage).
			// Colour and depth are both in SHADER_READ_ONLY here (every branch above
			// ends that way), in the still-multisampled scene image (the MSAA resolve
			// is below), so soft particles are included in that resolve.
			if (useSoftParticles && spriteRenderer) {
				// Move colour back to COLOR_ATTACHMENT for the sprite pass; depth stays
				// SHADER_READ for sampling.
				VkImageMemoryBarrier toColor{};
				toColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				toColor.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toColor.image = offscreenColor->GetImage();
				toColor.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
				toColor.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					0, 0, nullptr, 0, nullptr, 1, &toColor);

				// Begin sprite render pass (color-only, LOAD preserves the scene)
				VkRenderPassBeginInfo spriteRenderPassInfo{};
				spriteRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				spriteRenderPassInfo.renderPass = framebufferManager->GetSpriteRenderPass();
				spriteRenderPassInfo.framebuffer = framebufferManager->GetSpriteFramebuffer();
				spriteRenderPassInfo.renderArea.offset = {0, 0};
				spriteRenderPassInfo.renderArea.extent = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};
				spriteRenderPassInfo.clearValueCount = 0;
				spriteRenderPassInfo.pClearValues = nullptr;

				vkCmdBeginRenderPass(commandBuffer, &spriteRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport spriteViewport{};
				spriteViewport.x = 0.0f;
				spriteViewport.y = static_cast<float>(renderHeight);
				spriteViewport.width = static_cast<float>(renderWidth);
				spriteViewport.height = -static_cast<float>(renderHeight);
				spriteViewport.minDepth = 0.0f;
				spriteViewport.maxDepth = 1.0f;
				vkCmdSetViewport(commandBuffer, 0, 1, &spriteViewport);

				VkRect2D spriteScissor{};
				spriteScissor.offset = {0, 0};
				spriteScissor.extent = {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)};
				vkCmdSetScissor(commandBuffer, 0, 1, &spriteScissor);

				spriteRenderer->Render(commandBuffer, imageIndex);

				vkCmdEndRenderPass(commandBuffer);

				// Back to SHADER_READ_ONLY for the MSAA resolve / post-process chain.
				VkImageMemoryBarrier toRead{};
				toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				toRead.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toRead.image = offscreenColor->GetImage();
				toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
				toRead.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &toRead);

				spriteRenderer->Clear();
			}

			// Under MSAA, resolve the final multisampled scene colour for the
			// post-process chain. offscreenColor is in SHADER_READ_ONLY_OPTIMAL here
			// (every branch above ends that way). Depth was already resolved right
			// after the opaque pass (it doesn't change afterward), so only colour is
			// resolved now. After this, offscreenColor/offscreenDepth refer to the
			// resolved images and the depth-reading filters pick them up via
			// GetResolvedDepthImage().
			if (msaaScene) {
				framebufferManager->ResolveScene(commandBuffer,
				                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				offscreenColor = framebufferManager->GetResolvedColorImage();
				offscreenDepth = framebufferManager->GetResolvedDepthImage();
			}

			// --- Post-process ping-pong setup ---
			// currentInput: image in SHADER_READ_ONLY_OPTIMAL, consumed by each filter.
			// currentOutput: image filters render into; each filter's render pass transitions
			//   it UNDEFINED → COLOR_ATTACHMENT (during) → SHADER_READ_ONLY (on vkCmdEndRenderPass).
			// After each filter: swap(currentInput, currentOutput); no explicit barrier needed
			//   between filters because the render pass finalLayout handles SHADER_READ_ONLY for
			//   the new currentInput, and initialLayout=UNDEFINED accepts any layout for currentOutput.
			// The chain ping-pongs between two scratch images. It deliberately
			// never renders INTO offscreenColor: that image is the resolved
			// scene, i.e. the chain's own first input, the target the final
			// result is mirrored into for screenshots, and the destination
			// ResolveScene() writes each frame. Using it as an intermediate
			// render target made the result depend on the PARITY of the number
			// of active filters -- with an even count the chain ended in it,
			// and the frame intermittently came out corrupt; removing any one
			// filter (bloom, DoF, lens flare, or auto-exposure via r_hdr)
			// flipped the parity and hid the problem, while disabling camera
			// blur did not, because with a still camera that filter never runs.
			VulkanImage* currentInput = offscreenColor.GetPointerOrNull();
			Handle<VulkanImage> ppTempImage, ppTempImage2;
			if (temporaryImagePool && framebufferManager) {
				ppTempImage = temporaryImagePool->Acquire(
					static_cast<uint32_t>(renderWidth),
					static_cast<uint32_t>(renderHeight),
					framebufferManager->GetMainColorFormat()
				);
				ppTempImage2 = temporaryImagePool->Acquire(
					static_cast<uint32_t>(renderWidth),
					static_cast<uint32_t>(renderHeight),
					framebufferManager->GetMainColorFormat()
				);
			}
			VulkanImage* currentOutput = ppTempImage.GetPointerOrNull();

			// Advance the ping-pong after a filter has run. The just-written
			// image becomes the next input; the next output is whichever
			// scratch image is not now the input, so offscreenColor is only
			// ever read.
			VulkanImage* const ppA = ppTempImage.GetPointerOrNull();
			VulkanImage* const ppB = ppTempImage2.GetPointerOrNull();
			auto advanceChain = [&]() {
				currentInput = currentOutput;
				currentOutput = (currentInput == ppA) ? ppB : ppA;
			};

			// Order matches GL (GLRenderer.cpp:922-1037):
			//   Fog2 → DoF → Bloom → FXAA → LensFlare → AutoExposure(+tonemap)
			// AutoExposure runs LAST so its histogram samples the fully painted
			// scene (in-scattered sky, bloom, sun glare, etc.). Running it first
			// would sample the bare scene where the sky is still black (Sky.frag
			// is skipped under r_fogShadow>=1), causing the gain to clamp high
			// and the whole frame to come out over-bright.

			// Fog shadow / atmospheric in-scatter
			if ((int)r_fogShadow && fogFilter && mapShadowRenderer && currentInput && currentOutput) {
				fogFilter->Filter(commandBuffer, currentInput, currentOutput);
				advanceChain();
			}

			// Depth of Field
			if ((int)r_depthOfField && depthOfFieldFilter && currentInput && currentOutput) {
				const client::SceneDefinition& def = GetSceneDef();
				if (def.depthOfFieldFocalLength > 0.0f || def.blurVignette > 0.0f) {
					depthOfFieldFilter->Filter(commandBuffer, currentInput, currentOutput);
					advanceChain();
				}
			}

			// Camera motion blur (+ radial blur). GL order: DoF -> CameraBlur
			// -> Bloom. Apply() returns false when skipped (no motion), in
			// which case currentInput passes through unchanged.
			{
				const client::SceneDefinition& def = GetSceneDef();
				if ((float)r_cameraBlur > 0.0f && !def.denyCameraBlur && cameraBlurFilter &&
				    currentInput && currentOutput) {
					float intensity = std::min((float)r_cameraBlur * 0.2f, 1.0f);
					if (cameraBlurFilter->Apply(commandBuffer, currentInput, currentOutput,
					                            intensity, def.radialBlur))
						advanceChain();
				}
			}

			// Bloom
			if ((int)r_bloom && bloomFilter && currentInput && currentOutput) {
				bloomFilter->Filter(commandBuffer, currentInput, currentOutput);
				advanceChain();
			}

			// FXAA
			if ((int)r_fxaa && fxaaFilter && currentInput && currentOutput) {
				fxaaFilter->Filter(commandBuffer, currentInput, currentOutput);
				advanceChain();
			}

			// Sun lens flare. Runs after FXAA (so the flare quads aren't
			// smeared by anti-aliasing) and before auto-exposure (so the
			// bright sun contributes to the histogram, mirroring GL).
			if ((int)r_lensFlare && lensFlareFilter && currentInput && currentOutput) {
				lensFlareFilter->Filter(commandBuffer, currentInput, currentOutput);
				advanceChain();
			}

			// Auto-exposure + tonemap (HDR only) — runs LAST, sampling the
			// fully composed scene to compute its gain.
			if ((int)r_hdr && autoExposureFilter && currentInput && currentOutput) {
				autoExposureFilter->Filter(commandBuffer, currentInput, currentOutput, lastDt);
				advanceChain();
			}

			// Colour correction: white-balance tint, saturation desat,
			// ACES filmic tonemap, enhancement smoothstep. Mirrors GL's
			// GLColorCorrectionFilter and is the actual HDR compression
			// step — without it the scene retains its full linear range
			// and a saturated fog-tinted cast.
			if ((int)r_colorCorrection && colorCorrectionFilter && currentInput && currentOutput) {
				colorCorrectionFilter->Filter(commandBuffer, currentInput, currentOutput);
				advanceChain();
			}

			// Screen-space cavity / silhouette outline. Runs last so the
			// outline is drawn on top of all colour grading at exactly screen-
			// pixel boundaries. Reads the offscreen depth attachment that the
			// fog / soft-particle path already transitions to SHADER_READ_ONLY
			// above; takes its own distance fade so it works whether or not
			// the fog filter is enabled / correct.
			if ((int)r_outlines && cavityOutlineFilter && currentInput && currentOutput) {
				cavityOutlineFilter->Filter(commandBuffer, currentInput, currentOutput);
				advanceChain();
			}

			lights.clear();

			// --- Resample + blit final post-process result to swapchain ---
			// r_scaleFilter: 0 = nearest, 1 = bilinear (blit filter),
			// 2 = bicubic via VulkanResampleBicubicFilter to a swapchain-sized
			// temp, then a 1:1 blit. Mirrors GLRenderer.cpp:1073.
			// currentInput stays the render-res image for the screenshot
			// mirror below; blitSrc is what actually reaches the swapchain.
			VulkanImage* blitSrc = currentInput;
			Handle<VulkanImage> resampledImage;
			VkFilter blitFilter = VK_FILTER_LINEAR;
			{
				VkExtent2D scExtent = device->GetSwapchainExtent();
				int sf = (int)r_scaleFilter;
				if (sf == 0) {
					blitFilter = VK_FILTER_NEAREST;
				} else if (sf == 2 && resampleBicubicFilter && temporaryImagePool &&
				           currentInput &&
				           ((uint32_t)renderWidth != scExtent.width ||
				            (uint32_t)renderHeight != scExtent.height)) {
					resampledImage = temporaryImagePool->Acquire(
					    scExtent.width, scExtent.height,
					    framebufferManager->GetMainColorFormat());
					if (resampledImage) {
						resampleBicubicFilter->Filter(commandBuffer, currentInput,
						                              resampledImage.GetPointerOrNull());
						blitSrc = resampledImage.GetPointerOrNull();
					}
				}
			}

			// Transition blit source from SHADER_READ_ONLY to TRANSFER_SRC
			VkImageMemoryBarrier barrier1{};
			barrier1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier1.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier1.image = blitSrc->GetImage();
			barrier1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier1.subresourceRange.baseMipLevel = 0;
			barrier1.subresourceRange.levelCount = 1;
			barrier1.subresourceRange.baseArrayLayer = 0;
			barrier1.subresourceRange.layerCount = 1;
			barrier1.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier1.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier1);

			// Transition swapchain image for transfer destination
			VkImageMemoryBarrier barrier2{};
			barrier2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier2.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier2.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier2.image = device->GetSwapchainImage(imageIndex);
			barrier2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier2.subresourceRange.baseMipLevel = 0;
			barrier2.subresourceRange.levelCount = 1;
			barrier2.subresourceRange.baseArrayLayer = 0;
			barrier2.subresourceRange.layerCount = 1;
			barrier2.srcAccessMask = 0;
			barrier2.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			// TOP_OF_PIPE as the source scope would mean "wait for nothing", so
			// this layout transition could run before the acquire semaphore has
			// been waited on -- a write to an image the presentation engine may
			// still be reading. Scoping it to TRANSFER puts it after the wait,
			// which the submit performs at TRANSFER | COLOR_ATTACHMENT_OUTPUT.
			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier2);

			// Blit final post-process image to swapchain
			VkImageBlit blitRegion{};
			blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.srcSubresource.layerCount = 1;
			blitRegion.srcOffsets[0] = {0, 0, 0};
			blitRegion.srcOffsets[1] = {(int32_t)blitSrc->GetWidth(),
			                            (int32_t)blitSrc->GetHeight(), 1};
			blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.dstSubresource.layerCount = 1;
			blitRegion.dstOffsets[0] = {0, 0, 0};
			blitRegion.dstOffsets[1] = {static_cast<int32_t>(device->GetSwapchainExtent().width),
			                            static_cast<int32_t>(device->GetSwapchainExtent().height), 1};

			vkCmdBlitImage(commandBuffer,
				blitSrc->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				device->GetSwapchainImage(imageIndex), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blitRegion, blitFilter);

			// If the bicubic pass ran, currentInput was never transitioned to
			// TRANSFER_SRC; do it now for the screenshot mirror copy below.
			if (blitSrc != currentInput) {
				VkImageMemoryBarrier miBarrier = barrier1;
				miBarrier.image = currentInput->GetImage();
				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &miBarrier);
			}

			// Mirror the final post-process result into renderColorImage so
			// ReadBitmap (screenshot capture) sees what the player sees. The
			// post-process chain ping-pongs between offscreenColor and a temp
			// image; with an odd number of filters (e.g. only Fog), the final
			// frame lives in the temp image and renderColorImage still holds
			// the pre-post-process scene. Skip this copy if no filter ran.
			if (currentInput != offscreenColor.GetPointerOrNull()) {
				VkImageMemoryBarrier toDst{};
				toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				toDst.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toDst.image = offscreenColor->GetImage();
				toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
				toDst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &toDst);

				VkImageCopy copyRegion{};
				copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copyRegion.srcSubresource.layerCount = 1;
				copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copyRegion.dstSubresource.layerCount = 1;
				copyRegion.extent = {(uint32_t)renderWidth, (uint32_t)renderHeight, 1};
				vkCmdCopyImage(commandBuffer,
					currentInput->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					offscreenColor->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &copyRegion);

				VkImageMemoryBarrier toRead{};
				toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toRead.image = offscreenColor->GetImage();
				toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
				toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				vkCmdPipelineBarrier(commandBuffer,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &toRead);
			}

			// Transition currentInput back to SHADER_READ_ONLY for next frame
			barrier1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier1.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier1.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier1);

			// Transition swapchain image to color attachment for UI rendering
			barrier2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier2.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			barrier2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier2.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier2);
		}

		// Begin swapchain render pass for 2D UI rendering.
		// Scene frames LOAD the blitted scene colour (swapchain already in
		// COLOR_ATTACHMENT_OPTIMAL, no clear needed); 2D-only frames CLEAR the
		// colour from UNDEFINED via the renderPass2D variant.
		VkClearValue swapchainClearValue{};
		swapchainClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

		VkRenderPassBeginInfo swapchainRenderPassInfo{};
		swapchainRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		swapchainRenderPassInfo.renderPass = sceneUsedInThisFrame ? renderPass : renderPass2D;
		swapchainRenderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
		swapchainRenderPassInfo.renderArea.offset = {0, 0};
		swapchainRenderPassInfo.renderArea.extent = device->GetSwapchainExtent();
		swapchainRenderPassInfo.clearValueCount = sceneUsedInThisFrame ? 0 : 1;
		swapchainRenderPassInfo.pClearValues = sceneUsedInThisFrame ? nullptr : &swapchainClearValue;

		vkCmdBeginRenderPass(commandBuffer, &swapchainRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Render fullscreen multiply-color tints (hit flash, underwater tint, etc.)
		RenderMultiplyColors(commandBuffer);

		// Flush image renderer to record 2D drawing commands (UI)
		if (imageRenderer) {
			imageRenderer->Flush(commandBuffer, imageIndex);
		}

		vkCmdEndRenderPass(commandBuffer);

			vkEndCommandBuffer(commandBuffer);
		}

		void VulkanRenderer::CreateSkyPipeline() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Load SPIR-V shaders
			auto LoadSPIRVFile = [](const char* filename) -> std::vector<uint32_t> {
				return SpirvCache::Load(filename);
			};

			std::vector<uint32_t> vertCode = LoadSPIRVFile("Shaders/Vulkan/Sky.vert.spv");
			std::vector<uint32_t> fragCode = LoadSPIRVFile("Shaders/Vulkan/Sky.frag.spv");

			// Create shader modules
			VkShaderModuleCreateInfo vertShaderModuleInfo{};
			vertShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			vertShaderModuleInfo.codeSize = vertCode.size() * sizeof(uint32_t);
			vertShaderModuleInfo.pCode = vertCode.data();

			VkShaderModule vertShaderModule;
			VkResult result = vkCreateShaderModule(vkDevice, &vertShaderModuleInfo, nullptr, &vertShaderModule);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create sky vertex shader module (error code: %d)", result);
			}

			VkShaderModuleCreateInfo fragShaderModuleInfo{};
			fragShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			fragShaderModuleInfo.codeSize = fragCode.size() * sizeof(uint32_t);
			fragShaderModuleInfo.pCode = fragCode.data();

			VkShaderModule fragShaderModule;
			result = vkCreateShaderModule(vkDevice, &fragShaderModuleInfo, nullptr, &fragShaderModule);
			if (result != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				SPRaise("Failed to create sky fragment shader module (error code: %d)", result);
			}

			// Shader stages
			VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
			vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertShaderStageInfo.module = vertShaderModule;
			vertShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
			fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragShaderStageInfo.module = fragShaderModule;
			fragShaderStageInfo.pName = "main";

			VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

			// Vertex input - 2D position
			VkVertexInputBindingDescription bindingDescription{};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(float) * 2;
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attributeDescription{};
			attributeDescription.binding = 0;
			attributeDescription.location = 0;
			attributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
			attributeDescription.offset = 0;

			VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 1;
			vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
			vertexInputInfo.vertexAttributeDescriptionCount = 1;
			vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

			// Input assembly
			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			// Viewport state (dynamic)
			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;

			// Rasterization
			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_NONE;
			rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_FALSE;

			// Multisampling
			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.sampleShadingEnable = VK_FALSE;
			// Match the scene render pass sample count (MSAA).
			multisampling.rasterizationSamples = device->GetSampleCount();

			// Depth stencil - no depth test, sky is always in background
			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_FALSE;
			depthStencil.depthWriteEnable = VK_FALSE;
			depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
			depthStencil.depthBoundsTestEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			// Color blending
			VkPipelineColorBlendAttachmentState colorBlendAttachment{};
			colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			colorBlendAttachment.blendEnable = VK_FALSE;

			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.logicOpEnable = VK_FALSE;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &colorBlendAttachment;

			// Dynamic state
			VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			// Push constants for sky parameters
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pushConstantRange.offset = 0;
			pushConstantRange.size = sizeof(SkyPushConstants);

			VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
			pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			pipelineLayoutInfo.setLayoutCount = 0;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

			result = vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &skyPipelineLayout);
			if (result != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);
				SPRaise("Failed to create sky pipeline layout (error code: %d)", result);
			}

			// Create graphics pipeline
			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = shaderStages;
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = skyPipelineLayout;
			pipelineInfo.renderPass = framebufferManager->GetRenderPass(); // Use offscreen render pass for 3D rendering
			pipelineInfo.subpass = 0;

			result = vkCreateGraphicsPipelines(vkDevice, GetPipelineCache(), 1, &pipelineInfo, nullptr, &skyPipeline);

			vkDestroyShaderModule(vkDevice, vertShaderModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragShaderModule, nullptr);

			if (result != VK_SUCCESS) {
				SPRaise("Failed to create sky graphics pipeline (error code: %d)", result);
			}

			// Create fullscreen quad buffers
			struct Vertex {
				float x, y;
			};

			Vertex vertices[] = {
				{-1.0f, -1.0f},
				{1.0f, -1.0f},
				{-1.0f, 1.0f},
				{1.0f, 1.0f}
			};

			uint16_t indices[] = {0, 1, 2, 2, 1, 3};

			skyVertexBuffer = Handle<VulkanBuffer>::New(
				device,
				sizeof(vertices),
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);
			skyVertexBuffer->UpdateData(vertices, sizeof(vertices));

			skyIndexBuffer = Handle<VulkanBuffer>::New(
				device,
				sizeof(indices),
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			);
			skyIndexBuffer->UpdateData(indices, sizeof(indices));

			SPLog("Sky pipeline created successfully");
		}

		void VulkanRenderer::DestroySkyPipeline() {
			VkDevice vkDevice = device->GetDevice();

			if (skyPipeline != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, skyPipeline, nullptr);
				skyPipeline = VK_NULL_HANDLE;
			}

			if (skyPipelineLayout != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, skyPipelineLayout, nullptr);
				skyPipelineLayout = VK_NULL_HANDLE;
			}

			skyVertexBuffer.Set(nullptr, false);
			skyIndexBuffer.Set(nullptr, false);
		}

		void VulkanRenderer::CreateMultiplyColorPipeline() {
			SPADES_MARK_FUNCTION();

			VkDevice vkDevice = device->GetDevice();

			// Load shaders
			auto LoadSPIRVFile = [](const char* filename) -> std::vector<uint32_t> {
				return SpirvCache::Load(filename);
			};

			std::vector<uint32_t> vertCode = LoadSPIRVFile("Shaders/Vulkan/MultiplyColor.vert.spv");
			std::vector<uint32_t> fragCode = LoadSPIRVFile("Shaders/Vulkan/MultiplyColor.frag.spv");

			VkShaderModuleCreateInfo vertInfo{};
			vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			vertInfo.codeSize = vertCode.size() * sizeof(uint32_t);
			vertInfo.pCode = vertCode.data();
			VkShaderModule vertModule;
			if (vkCreateShaderModule(vkDevice, &vertInfo, nullptr, &vertModule) != VK_SUCCESS) {
				SPRaise("Failed to create MultiplyColor vertex shader module");
			}

			VkShaderModuleCreateInfo fragInfo{};
			fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			fragInfo.codeSize = fragCode.size() * sizeof(uint32_t);
			fragInfo.pCode = fragCode.data();
			VkShaderModule fragModule;
			if (vkCreateShaderModule(vkDevice, &fragInfo, nullptr, &fragModule) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertModule, nullptr);
				SPRaise("Failed to create MultiplyColor fragment shader module");
			}

			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vertModule;
			stages[0].pName = "main";
			stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fragModule;
			stages[1].pName = "main";

			// No vertex input — fullscreen triangle generated from gl_VertexIndex
			VkPipelineVertexInputStateCreateInfo vertexInput{};
			vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;

			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_NONE;
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			// No depth test — this is a 2D post-process over the swapchain image
			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_FALSE;
			depthStencil.depthWriteEnable = VK_FALSE;

			// Multiplicative blend: dst = dst * src_color  (ZERO, SRC_COLOR)
			VkPipelineColorBlendAttachmentState blendAttachment{};
			blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			blendAttachment.blendEnable = VK_TRUE;
			blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
			blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
			blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &blendAttachment;

			VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			// Push constant: vec3 color (fragment stage)
			VkPushConstantRange pushConstantRange{};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			pushConstantRange.offset = 0;
			pushConstantRange.size = sizeof(float) * 3;

			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges = &pushConstantRange;

			VkResult result = vkCreatePipelineLayout(vkDevice, &layoutInfo, nullptr, &multiplyColorPipelineLayout);
			if (result != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragModule, nullptr);
				SPRaise("Failed to create MultiplyColor pipeline layout (error: %d)", result);
			}

			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = stages;
			pipelineInfo.pVertexInputState = &vertexInput;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = multiplyColorPipelineLayout;
			pipelineInfo.renderPass = renderPass;
			pipelineInfo.subpass = 0;

			result = vkCreateGraphicsPipelines(vkDevice, GetPipelineCache(), 1, &pipelineInfo, nullptr, &multiplyColorPipeline);

			vkDestroyShaderModule(vkDevice, vertModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragModule, nullptr);

			if (result != VK_SUCCESS) {
				SPRaise("Failed to create MultiplyColor pipeline (error: %d)", result);
			}
		}

		void VulkanRenderer::DestroyMultiplyColorPipeline() {
			VkDevice vkDevice = device->GetDevice();
			if (multiplyColorPipeline != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, multiplyColorPipeline, nullptr);
				multiplyColorPipeline = VK_NULL_HANDLE;
			}
			if (multiplyColorPipelineLayout != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, multiplyColorPipelineLayout, nullptr);
				multiplyColorPipelineLayout = VK_NULL_HANDLE;
			}
		}

		void VulkanRenderer::RenderMultiplyColors(VkCommandBuffer commandBuffer) {
			if (pendingMultiplyColors.empty() || multiplyColorPipeline == VK_NULL_HANDLE) {
				pendingMultiplyColors.clear();
				return;
			}

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, multiplyColorPipeline);

			VkExtent2D extent = device->GetSwapchainExtent();
			VkViewport viewport{};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = static_cast<float>(extent.width);
			viewport.height = static_cast<float>(extent.height);
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor{};
			scissor.offset = {0, 0};
			scissor.extent = extent;
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			for (const Vector3& color : pendingMultiplyColors) {
				float pushConstants[3] = {color.x, color.y, color.z};
				vkCmdPushConstants(commandBuffer, multiplyColorPipelineLayout,
				                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), pushConstants);
				vkCmdDraw(commandBuffer, 3, 1, 0, 0);
			}

			pendingMultiplyColors.clear();
		}

		void VulkanRenderer::RenderSky(VkCommandBuffer commandBuffer) {
			if (skyPipeline == VK_NULL_HANDLE) {
				return;
			}

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);

			// Push constants: fogColor, viewAxisFront, viewAxisUp, viewAxisSide, fovX, fovY
			SkyPushConstants pushConstants;

			// Sky uses the actual fog color, not the shadow-modified one.
			// Linearize so it matches the convention used by BasicMap/BasicModel
			// (their C++ also squares fogColor before pushing). The offscreen
			// target is linear UNORM and the swapchain blit applies sRGB
			// encoding on output, so we must write linear values here too.
			Vector3 skyFogColor = fogColor;
			skyFogColor *= skyFogColor;
			pushConstants.fogColor[0] = skyFogColor.x;
			pushConstants.fogColor[1] = skyFogColor.y;
			pushConstants.fogColor[2] = skyFogColor.z;

			pushConstants.viewAxisFront[0] = sceneDef.viewAxis[2].x;
			pushConstants.viewAxisFront[1] = sceneDef.viewAxis[2].y;
			pushConstants.viewAxisFront[2] = sceneDef.viewAxis[2].z;

			pushConstants.viewAxisUp[0] = sceneDef.viewAxis[1].x;
			pushConstants.viewAxisUp[1] = sceneDef.viewAxis[1].y;
			pushConstants.viewAxisUp[2] = sceneDef.viewAxis[1].z;

			pushConstants.viewAxisSide[0] = sceneDef.viewAxis[0].x;
			pushConstants.viewAxisSide[1] = sceneDef.viewAxis[0].y;
			pushConstants.viewAxisSide[2] = sceneDef.viewAxis[0].z;

			pushConstants.fovX = sceneDef.fovX;
			pushConstants.fovY = sceneDef.fovY;

			vkCmdPushConstants(
				commandBuffer,
				skyPipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0,
				sizeof(pushConstants),
				&pushConstants
			);

			VkBuffer vertexBuffers[] = {skyVertexBuffer->GetBuffer()};
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffer, skyIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

			vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);
		}

		void VulkanRenderer::CreateDebugLinePipeline() {
			VkDevice vkDevice = device->GetDevice();

			// Load shaders
			auto LoadSPV = [](const char* path) -> std::vector<uint32_t> {
				return SpirvCache::Load(path);
			};

			auto vertCode = LoadSPV("Shaders/Vulkan/DebugLine.vert.spv");
			auto fragCode = LoadSPV("Shaders/Vulkan/DebugLine.frag.spv");

			VkShaderModuleCreateInfo vertInfo{};
			vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			vertInfo.codeSize = vertCode.size() * sizeof(uint32_t);
			vertInfo.pCode = vertCode.data();
			VkShaderModule vertModule;
			if (vkCreateShaderModule(vkDevice, &vertInfo, nullptr, &vertModule) != VK_SUCCESS) {
				SPLog("Warning: Failed to create debug line vertex shader module");
				return;
			}

			VkShaderModuleCreateInfo fragInfo{};
			fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			fragInfo.codeSize = fragCode.size() * sizeof(uint32_t);
			fragInfo.pCode = fragCode.data();
			VkShaderModule fragModule;
			if (vkCreateShaderModule(vkDevice, &fragInfo, nullptr, &fragModule) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertModule, nullptr);
				SPLog("Warning: Failed to create debug line fragment shader module");
				return;
			}

			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vertModule;
			stages[0].pName = "main";
			stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = fragModule;
			stages[1].pName = "main";

			// Vertex input: vec3 position (loc 0), vec4 color (loc 1)
			VkVertexInputBindingDescription binding{};
			binding.binding = 0;
			binding.stride = sizeof(float) * 7; // vec3 + vec4
			binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attrs[2]{};
			attrs[0].binding = 0;
			attrs[0].location = 0;
			attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attrs[0].offset = 0;
			attrs[1].binding = 0;
			attrs[1].location = 1;
			attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			attrs[1].offset = sizeof(float) * 3;

			VkPipelineVertexInputStateCreateInfo vertexInput{};
			vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInput.vertexBindingDescriptionCount = 1;
			vertexInput.pVertexBindingDescriptions = &binding;
			vertexInput.vertexAttributeDescriptionCount = 2;
			vertexInput.pVertexAttributeDescriptions = attrs;

			VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
			inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			inputAssembly.primitiveRestartEnable = VK_FALSE;

			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.viewportCount = 1;
			viewportState.scissorCount = 1;

			VkPipelineRasterizationStateCreateInfo rasterizer{};
			rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_NONE;
			rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

			VkPipelineMultisampleStateCreateInfo multisampling{};
			multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			// Match the scene render pass sample count (MSAA) — debug lines draw
			// into the offscreen scene pass.
			multisampling.rasterizationSamples = device->GetSampleCount();

			VkPipelineDepthStencilStateCreateInfo depthStencil{};
			depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			depthStencil.depthTestEnable = VK_FALSE;
			depthStencil.depthWriteEnable = VK_FALSE;
			depthStencil.stencilTestEnable = VK_FALSE;

			VkPipelineColorBlendAttachmentState blendAttachment{};
			blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			blendAttachment.blendEnable = VK_TRUE;
			blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.attachmentCount = 1;
			colorBlending.pAttachments = &blendAttachment;

			VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
			VkPipelineDynamicStateCreateInfo dynamicState{};
			dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dynamicState.dynamicStateCount = 2;
			dynamicState.pDynamicStates = dynamicStates;

			VkPushConstantRange pcRange{};
			pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			pcRange.offset = 0;
			pcRange.size = sizeof(Matrix4);

			VkPipelineLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges = &pcRange;

			if (vkCreatePipelineLayout(vkDevice, &layoutInfo, nullptr, &debugLinePipelineLayout) != VK_SUCCESS) {
				vkDestroyShaderModule(vkDevice, vertModule, nullptr);
				vkDestroyShaderModule(vkDevice, fragModule, nullptr);
				SPLog("Warning: Failed to create debug line pipeline layout");
				return;
			}

			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = stages;
			pipelineInfo.pVertexInputState = &vertexInput;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = debugLinePipelineLayout;
			pipelineInfo.renderPass = GetOffscreenRenderPass();
			pipelineInfo.subpass = 0;

			VkResult result = vkCreateGraphicsPipelines(vkDevice, GetPipelineCache(), 1, &pipelineInfo, nullptr, &debugLinePipeline);

			vkDestroyShaderModule(vkDevice, vertModule, nullptr);
			vkDestroyShaderModule(vkDevice, fragModule, nullptr);

			if (result != VK_SUCCESS) {
				SPLog("Warning: Failed to create debug line pipeline (error code: %d)", result);
				debugLinePipeline = VK_NULL_HANDLE;
			} else {
				SPLog("Debug line pipeline created successfully");
			}
		}

		void VulkanRenderer::DestroyDebugLinePipeline() {
			VkDevice vkDevice = device->GetDevice();
			if (debugLinePipeline != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyPipeline(vkDevice, debugLinePipeline, nullptr);
				debugLinePipeline = VK_NULL_HANDLE;
			}
			if (debugLinePipelineLayout != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(vkDevice, debugLinePipelineLayout, nullptr);
				debugLinePipelineLayout = VK_NULL_HANDLE;
			}
		}

		void VulkanRenderer::RenderDebugLines(VkCommandBuffer commandBuffer) {
			if (debugLines.empty() || debugLinePipeline == VK_NULL_HANDLE)
				return;

			// Build flat vertex buffer: 2 vertices per line, each vertex = vec3 pos + vec4 color
			struct LineVertex { float x, y, z, r, g, b, a; };
			std::vector<LineVertex> vertices;
			vertices.reserve(debugLines.size() * 2);
			for (const auto& line : debugLines) {
				vertices.push_back({line.v1.x, line.v1.y, line.v1.z, line.color.x, line.color.y, line.color.z, line.color.w});
				vertices.push_back({line.v2.x, line.v2.y, line.v2.z, line.color.x, line.color.y, line.color.z, line.color.w});
			}

			Handle<VulkanBuffer> vertexBuffer(new VulkanBuffer(
				device,
				vertices.size() * sizeof(LineVertex),
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			));
			vertexBuffer->UpdateData(vertices.data(), vertices.size() * sizeof(LineVertex));

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debugLinePipeline);

			VkViewport viewport{0.0f, (float)renderHeight, (float)renderWidth, -(float)renderHeight, 0.0f, 1.0f};
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
			VkRect2D scissor{{0, 0}, {static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight)}};
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			const Matrix4& mvp = GetProjectionViewMatrix();
			vkCmdPushConstants(commandBuffer, debugLinePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
			                   0, sizeof(Matrix4), &mvp);

			VkBuffer vb = vertexBuffer->GetBuffer();
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, &offset);

			vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices.size()), 1, 0, 0);

			QueueBufferForDeletion(vertexBuffer);
		}

	} // namespace draw
} // namespace spades
