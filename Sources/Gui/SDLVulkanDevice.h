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

#include <ZeroSpades.h>

#if USE_VULKAN

#include <vector>
#include <vulkan/vulkan.h>
#include <Imports/SDL.h>
#include <Core/RefCountedObject.h>
#include <Draw/Vulkan/vk_mem_alloc.h>

namespace spades {
	namespace gui {

		class SDLVulkanDevice : public RefCountedObject {
			SDL_Window* window;
			int w, h;

			// Vulkan core objects
			VkInstance instance;
			VkSurfaceKHR surface;
			VkPhysicalDevice physicalDevice;
			VkDevice device;

			// Queue families and queues
			uint32_t graphicsQueueFamily;
			uint32_t presentQueueFamily;
			VkQueue graphicsQueue;
			VkQueue presentQueue;

			// Swapchain
			VkSwapchainKHR swapchain;
			VkFormat swapchainImageFormat;
			VkExtent2D swapchainExtent;
			std::vector<VkImage> swapchainImages;
			std::vector<VkImageView> swapchainImageViews;

			// Command pools and buffers
			VkCommandPool commandPool;

			// Synchronization primitives
			std::vector<VkSemaphore> imageAvailableSemaphores;
			std::vector<VkSemaphore> renderFinishedSemaphores;
			std::vector<VkFence> inFlightFences;
			std::vector<VkFence> imagesInFlight;
			uint32_t currentFrame;

			// VMA allocator
			VmaAllocator allocator;
			bool dedicatedAllocEnabled;
			bool bindMemory2Enabled;

			// Optional Vulkan device features. Each is true only if the physical
			// device reports support AND we requested it in CreateLogicalDevice.
			bool samplerAnisotropySupported{false};
			bool sampleRateShadingSupported{false};
			bool fillModeNonSolidSupported{false};
			bool wideLinesSupported{false};
			bool geometryShaderSupported{false};

			// MSAA sample counts. `maxUsableSampleCount` is the highest count the
			// device supports for both colour and depth framebuffer attachments;
			// `sampleCount` is the count actually selected from `r_multisamples`,
			// clamped down to what the device allows. Both are resolved once, when
			// the physical device is picked, and stay fixed for the session
			// (`r_multisamples` is a Latch setting).
			VkSampleCountFlagBits maxUsableSampleCount{VK_SAMPLE_COUNT_1_BIT};
			VkSampleCountFlagBits sampleCount{VK_SAMPLE_COUNT_1_BIT};

			// Swapchain generation counter, incremented on every successful recreation
		uint32_t swapchainGeneration{0};

		// Debug messenger (only in debug mode)
#ifndef NDEBUG
			VkDebugUtilsMessengerEXT debugMessenger;
#endif

			// Helper methods
			void CreateInstance();
			void SetupDebugMessenger();
			void CreateSurface();
			void PickPhysicalDevice();
			void ResolveSampleCount();
			void CreateLogicalDevice();
			void CreateAllocator();
			void CreateSwapchain();
			void CreateImageViews();
			void CreateCommandPool();
			void CreateSyncObjects();

			// Cleanup methods
			void CleanupSwapchain();

		protected:
			~SDLVulkanDevice();

		public:
			SDLVulkanDevice(SDL_Window* window);

			// Getters for Vulkan objects
			VkInstance GetInstance() const { return instance; }
			VkDevice GetDevice() const { return device; }
			VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; }
			VmaAllocator GetAllocator() const { return allocator; }
			VkQueue GetGraphicsQueue() const { return graphicsQueue; }
			VkQueue GetPresentQueue() const { return presentQueue; }
			VkSwapchainKHR GetSwapchain() const { return swapchain; }
			VkFormat GetSwapchainImageFormat() const { return swapchainImageFormat; }
			VkExtent2D GetSwapchainExtent() const { return swapchainExtent; }
			const std::vector<VkImageView>& GetSwapchainImageViews() const { return swapchainImageViews; }
			VkImage GetSwapchainImage(uint32_t index) const { return swapchainImages[index]; }
			VkCommandPool GetCommandPool() const { return commandPool; }
			uint32_t GetGraphicsQueueFamily() const { return graphicsQueueFamily; }

			// Frame management
			uint32_t AcquireNextImage(VkSemaphore* outImageAvailableSemaphore, VkSemaphore* outRenderFinishedSemaphore);
			void PresentImage(uint32_t imageIndex, VkSemaphore* waitSemaphores, uint32_t waitSemaphoreCount);
			void WaitForFences();

			// Frame index (cycles 0..MAX_FRAMES_IN_FLIGHT-1)
			uint32_t GetCurrentFrame() const { return currentFrame; }
			uint32_t GetMaxFramesInFlight() const { return 2; }

			// Screen dimensions
			int ScreenWidth() const { return w; }
			int ScreenHeight() const { return h; }

			// Swapchain recreation (for window resize)
			void RecreateSwapchain();

			// Returns a monotonically increasing counter, bumped on each swapchain recreation.
			// Callers can compare against a cached value to detect when dependent resources
			// (framebuffers, depth image) need to be rebuilt.
			uint32_t GetSwapchainGeneration() const { return swapchainGeneration; }

			// Optional Vulkan feature availability. Renderers must consult these
			// before relying on a feature so they can degrade gracefully on
			// implementations that lack it (notably MoltenVK on macOS).
			bool HasSamplerAnisotropy() const { return samplerAnisotropySupported; }
			bool HasSampleRateShading() const { return sampleRateShadingSupported; }
			bool HasFillModeNonSolid() const { return fillModeNonSolidSupported; }
			bool HasWideLines() const { return wideLinesSupported; }
			bool HasGeometryShader() const { return geometryShaderSupported; }

			// MSAA sample count selected for the offscreen scene attachments
			// (VK_SAMPLE_COUNT_1_BIT when MSAA is off). Scene render passes,
			// framebuffer images and pipeline multisample state must all agree on
			// this value. The highest count the hardware can actually use is
			// exposed separately for diagnostics.
			VkSampleCountFlagBits GetSampleCount() const { return sampleCount; }
			VkSampleCountFlagBits GetMaxUsableSampleCount() const { return maxUsableSampleCount; }
		};

	} // namespace gui
} // namespace spades

#endif // USE_VULKAN
