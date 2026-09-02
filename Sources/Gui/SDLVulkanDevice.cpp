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

#include <ZeroSpades.h>

#if USE_VULKAN

#include "SDLVulkanDevice.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Settings.h>
#include <algorithm>
#include <set>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <SDL2/SDL_vulkan.h>

SPADES_SETTING(r_multisamples);

#if defined(__APPLE__) && defined(__x86_64__)
// MoltenVK global-configuration API, used to disable MTLHeap on Intel Macs.
// vkGetMoltenVKConfigurationMVK lives in the private API header;
// vkSetMoltenVKConfigurationMVK is in the (still functional) deprecated header.
#include <MoltenVK/mvk_private_api.h>
#include <MoltenVK/mvk_deprecated_api.h>
#endif

namespace spades {
	namespace gui {

		static const int MAX_FRAMES_IN_FLIGHT = 2;

		// Validation layers are on by default in debug builds, and can be turned
		// on in any build by setting ZS_VULKAN_VALIDATION=1. Note that the layers
		// are injected by the Vulkan *loader*: they cannot load when MoltenVK is
		// linked directly, so this also needs a build configured with
		// -DZS_VULKAN_USE_LOADER=ON.
		static bool DetermineValidationLayersEnabled() {
#ifndef NDEBUG
			return true;
#else
			const char* env = std::getenv("ZS_VULKAN_VALIDATION");
			return env && env[0] && env[0] != '0';
#endif
		}

		static const bool enableValidationLayers = DetermineValidationLayersEnabled();

		static const std::vector<const char*> validationLayers = {
			"VK_LAYER_KHRONOS_validation"
		};

		static const std::vector<const char*> deviceExtensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME
		};

		// Debug callback for validation layers
		static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData) {

			if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
				SPLog("[Vulkan] %s", pCallbackData->pMessage);
			}

			return VK_FALSE;
		}

		SDLVulkanDevice::SDLVulkanDevice(SDL_Window* wnd)
		: window(wnd),
		  instance(VK_NULL_HANDLE),
		  surface(VK_NULL_HANDLE),
		  physicalDevice(VK_NULL_HANDLE),
		  device(VK_NULL_HANDLE),
		  graphicsQueue(VK_NULL_HANDLE),
		  presentQueue(VK_NULL_HANDLE),
		  swapchain(VK_NULL_HANDLE),
		  commandPool(VK_NULL_HANDLE),
		  currentFrame(0),
		  allocator(VK_NULL_HANDLE),
		  dedicatedAllocEnabled(false),
		  bindMemory2Enabled(false)
#ifndef NDEBUG
		  , debugMessenger(VK_NULL_HANDLE)
#endif  // NDEBUG
		{
			SPADES_MARK_FUNCTION();

			SDL_GetWindowSize(window, &w, &h);
			SPLog("Initializing Vulkan device (window size: %dx%d)", w, h);

			try {
				CreateInstance();
				SetupDebugMessenger();
				CreateSurface();
				PickPhysicalDevice();
				ResolveSampleCount();
				CreateLogicalDevice();
				CreateAllocator();
				CreateSwapchain();
				CreateImageViews();
				CreateCommandPool();
				CreateSyncObjects();

				SPLog("Vulkan device initialized successfully");
			} catch (...) {
				// Cleanup on failure
				if (device != VK_NULL_HANDLE) {
					vkDeviceWaitIdle(device);
				}
				throw;
			}
		}

		SDLVulkanDevice::~SDLVulkanDevice() {
			SPADES_MARK_FUNCTION();

			if (device != VK_NULL_HANDLE) {
				vkDeviceWaitIdle(device);

				// Cleanup per-frame sync objects. renderFinished semaphores are
				// per swapchain image and destroyed by CleanupSwapchain below.
				for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
					if (imageAvailableSemaphores[i] != VK_NULL_HANDLE)
						vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
					if (inFlightFences[i] != VK_NULL_HANDLE)
						vkDestroyFence(device, inFlightFences[i], nullptr);
				}

				if (commandPool != VK_NULL_HANDLE)
					vkDestroyCommandPool(device, commandPool, nullptr);

				CleanupSwapchain();

				if (allocator != VK_NULL_HANDLE) {
					vmaDestroyAllocator(allocator);
					allocator = VK_NULL_HANDLE;
				}

				vkDestroyDevice(device, nullptr);
			}

			if (surface != VK_NULL_HANDLE)
				vkDestroySurfaceKHR(instance, surface, nullptr);

#ifndef NDEBUG
			if (debugMessenger != VK_NULL_HANDLE) {
				auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
					vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
				if (func != nullptr) {
					func(instance, debugMessenger, nullptr);
				}
			}
#endif

			if (instance != VK_NULL_HANDLE)
				vkDestroyInstance(instance, nullptr);

			SPLog("Vulkan device destroyed");
		}

		void SDLVulkanDevice::CreateInstance() {
			SPADES_MARK_FUNCTION();

#if defined(__APPLE__) && defined(__x86_64__)
			// MoltenVK 1.4 defaults useMTLHeap to WHERE_SAFE, which on any
			// non-AMD GPU means "always use MTLHeapTypePlacement". Intel Macs
			// (HD 5000 / Iris Pro, Haswell) report GPU Family Mac 2 yet their
			// Metal driver rejects placement heaps, so vkAllocateMemory aborts
			// hard inside the driver ("Placement heap type is not supported").
			// Forcing NEVER routes allocations to plain MTLBuffer/MTLTexture and
			// avoids the crash; the lost memory-aliasing optimization is
			// irrelevant for this workload. Scoped to the x86_64 (Intel) build
			// only — the Apple Silicon build keeps heaps, where they are safe.
			//
			// The MVK_CONFIG_USE_MTLHEAP env var is read only once, when
			// MoltenVK first initializes its global config; the startup-screen
			// capability probe already created and destroyed a VkInstance before
			// we get here, so setenv() would be too late. Mutating the global
			// config directly works because each new VkInstance snapshots it,
			// and the instance we create just below picks up the override.
			{
				MVKConfiguration mvkConfig{};
				size_t configSize = sizeof(mvkConfig);
				VkResult cfgRes =
				    vkGetMoltenVKConfigurationMVK(VK_NULL_HANDLE, &mvkConfig, &configSize);
				if (cfgRes == VK_SUCCESS || cfgRes == VK_INCOMPLETE) {
					mvkConfig.useMTLHeap = MVK_CONFIG_USE_MTLHEAP_NEVER;
					// Intel Mac Metal requires 256-byte constant-buffer offset
					// alignment; MoltenVK's argument buffers sub-allocate descriptor
					// buffers at 16-byte offsets, so descriptors are read from wrong
					// offsets there. Bind descriptors discretely instead.
					mvkConfig.useMetalArgumentBuffers = VK_FALSE;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
					vkSetMoltenVKConfigurationMVK(VK_NULL_HANDLE, &mvkConfig, &configSize);
#pragma clang diagnostic pop
				}
			}
#endif

			VkApplicationInfo appInfo{};
			appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
			appInfo.pApplicationName = "OpenSpades";
			appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 5);
			appInfo.pEngineName = "OpenSpades";
			appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 5);
			appInfo.apiVersion = VK_API_VERSION_1_0;

			// Get required SDL extensions
			unsigned int sdlExtensionCount = 0;
			if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, nullptr)) {
				SPRaise("Failed to get SDL Vulkan extensions count: %s", SDL_GetError());
			}

			std::vector<const char*> extensions(sdlExtensionCount);
			if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, extensions.data())) {
				SPRaise("Failed to get SDL Vulkan extensions: %s", SDL_GetError());
			}

			// Add debug extension and check validation layer availability if validation layers are enabled
			bool useValidationLayers = enableValidationLayers;
			if (useValidationLayers) {
				// Check available instance layers
				uint32_t availableLayerCount = 0;
				vkEnumerateInstanceLayerProperties(&availableLayerCount, nullptr);
				std::vector<VkLayerProperties> availableLayers(availableLayerCount);
				if (availableLayerCount > 0) {
					vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayers.data());
				}

				for (const char* layerName : validationLayers) {
					bool found = false;
					for (const auto& layerProps : availableLayers) {
						if (strcmp(layerName, layerProps.layerName) == 0) {
							found = true;
							break;
						}
					}
					if (!found) {
						SPLog("Warning: Requested validation layer '%s' not available; disabling validation layers", layerName);
						useValidationLayers = false;
						break;
					}
				}

				if (useValidationLayers) {
					extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
				}
			}

			// MoltenVK is a "portability" implementation rather than a fully
			// conformant one. When the application talks to it directly this is
			// invisible, but behind the Vulkan loader (loader 1.3.216 and newer)
			// portability drivers are hidden unless the application opts in, and
			// vkCreateInstance fails with VK_ERROR_INCOMPATIBLE_DRIVER. Opt in
			// when the loader advertises the extension; a direct MoltenVK link
			// does not advertise it, so this is a no-op there.
			VkInstanceCreateFlags instanceFlags = 0;
			{
				uint32_t extCount = 0;
				vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
				std::vector<VkExtensionProperties> available(extCount);
				if (extCount > 0)
					vkEnumerateInstanceExtensionProperties(nullptr, &extCount, available.data());

				for (const auto& ext : available) {
					if (strcmp(ext.extensionName, "VK_KHR_portability_enumeration") == 0) {
						extensions.push_back("VK_KHR_portability_enumeration");
						// VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
						instanceFlags |= 0x00000001;
						SPLog("Enabling portability enumeration (running behind the Vulkan loader)");
						break;
					}
				}
			}

			VkInstanceCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			createInfo.flags = instanceFlags;
			createInfo.pApplicationInfo = &appInfo;
			createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
			createInfo.ppEnabledExtensionNames = extensions.data();

			// Enable validation layers in debug mode (if available)
			VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
			if (useValidationLayers) {
				createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
				createInfo.ppEnabledLayerNames = validationLayers.data();

				// Setup debug messenger creation info for instance creation/destruction
				debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
				debugCreateInfo.messageSeverity =
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
				debugCreateInfo.messageType =
					VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
				debugCreateInfo.pfnUserCallback = debugCallback;

				createInfo.pNext = &debugCreateInfo;
			} else {
				createInfo.enabledLayerCount = 0;
				createInfo.pNext = nullptr;
			}

			// On macOS with MoltenVK, enable portability enumeration if available.
			// When linking directly to MoltenVK the extension may not be present,
			// but MoltenVK still exposes its device without it.
#ifdef __APPLE__
			{
				uint32_t availableExtCount = 0;
				vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, nullptr);
				std::vector<VkExtensionProperties> availableExts(availableExtCount);
				if (availableExtCount > 0) {
					vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, availableExts.data());
				}

				bool hasPortabilityEnum = false;
				bool hasPhysDevProps2 = false;
				for (const auto& ext : availableExts) {
					if (strcmp(ext.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
						hasPortabilityEnum = true;
					if (strcmp(ext.extensionName, "VK_KHR_get_physical_device_properties2") == 0)
						hasPhysDevProps2 = true;
				}

				if (hasPortabilityEnum) {
					createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
					extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
				}
				if (hasPhysDevProps2) {
					extensions.push_back("VK_KHR_get_physical_device_properties2");
				}
				createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
				createInfo.ppEnabledExtensionNames = extensions.data();
			}
#endif

			VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create Vulkan instance (error code: %d)", result);
			}

			SPLog("Vulkan instance created");
		}

		void SDLVulkanDevice::SetupDebugMessenger() {
#ifndef NDEBUG
			if (!enableValidationLayers) return;

			VkDebugUtilsMessengerCreateInfoEXT createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			createInfo.messageSeverity =
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			createInfo.messageType =
				VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			createInfo.pfnUserCallback = debugCallback;

			auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
				vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

			if (func != nullptr) {
				VkResult result = func(instance, &createInfo, nullptr, &debugMessenger);
				if (result != VK_SUCCESS) {
					SPLog("Warning: Failed to set up debug messenger (error code: %d)", result);
				} else {
					SPLog("Vulkan debug messenger created");
				}
			}
#endif
		}

		void SDLVulkanDevice::CreateSurface() {
			if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
				SPRaise("Failed to create Vulkan surface: %s", SDL_GetError());
			}
			SPLog("Vulkan surface created");
		}

		void SDLVulkanDevice::PickPhysicalDevice() {
			uint32_t deviceCount = 0;
			vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

			if (deviceCount == 0) {
				SPRaise("Failed to find GPUs with Vulkan support");
			}

			std::vector<VkPhysicalDevice> devices(deviceCount);
			vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

			// For now, just pick the first suitable device
			// TODO: Implement device scoring to pick the best GPU
			for (const auto& dev : devices) {
				VkPhysicalDeviceProperties properties;
				vkGetPhysicalDeviceProperties(dev, &properties);

				// Check if device supports required queue families
				uint32_t queueFamilyCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);

				std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
				vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());

				bool foundGraphics = false;
				bool foundPresent = false;

				for (uint32_t i = 0; i < queueFamilies.size(); i++) {
					if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
						graphicsQueueFamily = i;
						foundGraphics = true;
					}

					VkBool32 presentSupport = false;
					vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &presentSupport);
					if (presentSupport) {
						presentQueueFamily = i;
						foundPresent = true;
					}

					if (foundGraphics && foundPresent) break;
				}

				// Check device extension support
				uint32_t extensionCount;
				vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, nullptr);

				std::vector<VkExtensionProperties> availableExtensions(extensionCount);
				vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount,
					availableExtensions.data());

				std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
				for (const auto& extension : availableExtensions) {
					requiredExtensions.erase(extension.extensionName);
				}

				if (foundGraphics && foundPresent && requiredExtensions.empty()) {
					physicalDevice = dev;
					SPLog("Selected GPU: %s", properties.deviceName);
					break;
				}
			}

			if (physicalDevice == VK_NULL_HANDLE) {
				SPRaise("Failed to find a suitable GPU");
			}
		}

		void SDLVulkanDevice::ResolveSampleCount() {
			SPADES_MARK_FUNCTION();

			// Highest sample count the device supports for *both* colour and depth
			// framebuffer attachments — the scene pass uses one count for both, so
			// we intersect the two masks.
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(physicalDevice, &props);
			VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts &
			                            props.limits.framebufferDepthSampleCounts;

			if (counts & VK_SAMPLE_COUNT_8_BIT)      maxUsableSampleCount = VK_SAMPLE_COUNT_8_BIT;
			else if (counts & VK_SAMPLE_COUNT_4_BIT) maxUsableSampleCount = VK_SAMPLE_COUNT_4_BIT;
			else if (counts & VK_SAMPLE_COUNT_2_BIT) maxUsableSampleCount = VK_SAMPLE_COUNT_2_BIT;
			else                                     maxUsableSampleCount = VK_SAMPLE_COUNT_1_BIT;

			// Map the requested r_multisamples (0/1 = off, 2/4/8 = MSAA) to a sample
			// flag, rounding down to a power of two, then clamp to what the hardware
			// can actually use. An unsupported request (e.g. 8x on a 4x device)
			// silently drops to the highest supported level rather than failing.
			int requested = (int)r_multisamples;
			VkSampleCountFlagBits desired;
			if (requested >= 8)      desired = VK_SAMPLE_COUNT_8_BIT;
			else if (requested >= 4) desired = VK_SAMPLE_COUNT_4_BIT;
			else if (requested >= 2) desired = VK_SAMPLE_COUNT_2_BIT;
			else                     desired = VK_SAMPLE_COUNT_1_BIT;

			sampleCount = static_cast<VkSampleCountFlagBits>(
			    std::min<uint32_t>(desired, maxUsableSampleCount));

			if (sampleCount != desired) {
				SPLog("MSAA: requested %dx clamped to %ux (device max %ux)",
				      requested, (unsigned)sampleCount, (unsigned)maxUsableSampleCount);
			} else if (sampleCount > VK_SAMPLE_COUNT_1_BIT) {
				SPLog("MSAA: using %ux", (unsigned)sampleCount);
			} else {
				SPLog("MSAA: disabled");
			}
		}

		void SDLVulkanDevice::CreateLogicalDevice() {
			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
			std::set<uint32_t> uniqueQueueFamilies = {graphicsQueueFamily, presentQueueFamily};

			float queuePriority = 1.0f;
			for (uint32_t queueFamily : uniqueQueueFamilies) {
				VkDeviceQueueCreateInfo queueCreateInfo{};
				queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueCreateInfo.queueFamilyIndex = queueFamily;
				queueCreateInfo.queueCount = 1;
				queueCreateInfo.pQueuePriorities = &queuePriority;
				queueCreateInfos.push_back(queueCreateInfo);
			}

			// Query supported features before enabling them
			VkPhysicalDeviceFeatures supportedFeatures{};
			vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);

			VkPhysicalDeviceFeatures deviceFeatures{};
			// Only enable features that the physical device reports as supported.
			// Each flag is also stored on the device so renderers can probe it
			// later via Has*() and degrade gracefully when missing.
			samplerAnisotropySupported = supportedFeatures.samplerAnisotropy == VK_TRUE;
			if (samplerAnisotropySupported) {
				deviceFeatures.samplerAnisotropy = VK_TRUE;
			} else {
				SPLog("Warning: Anisotropic filtering not supported on this device");
			}
			sampleRateShadingSupported = supportedFeatures.sampleRateShading == VK_TRUE;
			if (sampleRateShadingSupported) {
				deviceFeatures.sampleRateShading = VK_TRUE;
			} else {
				SPLog("Warning: Sample rate shading not supported on this device");
			}
			fillModeNonSolidSupported = supportedFeatures.fillModeNonSolid == VK_TRUE;
			if (fillModeNonSolidSupported) {
				deviceFeatures.fillModeNonSolid = VK_TRUE;
			} else {
				SPLog("Warning: fillModeNonSolid not supported - wireframe outlines unavailable");
			}
			wideLinesSupported = supportedFeatures.wideLines == VK_TRUE;
			if (wideLinesSupported) {
				deviceFeatures.wideLines = VK_TRUE;
			} else {
				SPLog("Warning: wideLines not supported - line outlines limited to 1px");
			}
			geometryShaderSupported = supportedFeatures.geometryShader == VK_TRUE;
			if (geometryShaderSupported) {
				deviceFeatures.geometryShader = VK_TRUE;
			} else {
				SPLog("Warning: geometryShader not supported - geom-shader paths disabled");
			}

			VkDeviceCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
			createInfo.pQueueCreateInfos = queueCreateInfos.data();
			createInfo.pEnabledFeatures = &deviceFeatures;

			// Device extensions (swapchain is required)
			std::vector<const char*> extensions = deviceExtensions;
#ifdef __APPLE__
			// MoltenVK requires portability subset extension
			extensions.push_back("VK_KHR_portability_subset");
#endif

			// Probe and enable optional extensions needed by VMA.
			// VK_KHR_dedicated_allocation requires VK_KHR_get_memory_requirements2.
			{
				uint32_t extCount = 0;
				vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
				std::vector<VkExtensionProperties> availExts(extCount);
				if (extCount > 0)
					vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, availExts.data());

				bool hasGetMemReq2 = false;
				bool hasDedicatedAlloc = false;
				bool hasBindMemory2 = false;
				for (const auto& ext : availExts) {
					if (strcmp(ext.extensionName, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME) == 0)
						hasGetMemReq2 = true;
					if (strcmp(ext.extensionName, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) == 0)
						hasDedicatedAlloc = true;
					if (strcmp(ext.extensionName, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME) == 0)
						hasBindMemory2 = true;
				}

				if (hasGetMemReq2 && hasDedicatedAlloc) {
					extensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
					extensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
					dedicatedAllocEnabled = true;
				}
				if (hasBindMemory2) {
					extensions.push_back(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
					bindMemory2Enabled = true;
				}
			}

			createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
			createInfo.ppEnabledExtensionNames = extensions.data();

			if (enableValidationLayers) {
				createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
				createInfo.ppEnabledLayerNames = validationLayers.data();
			} else {
				createInfo.enabledLayerCount = 0;
			}

			VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create logical device (error code: %d)", result);
			}

			vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
			vkGetDeviceQueue(device, presentQueueFamily, 0, &presentQueue);

			SPLog("Vulkan logical device created");
		}

		void SDLVulkanDevice::CreateAllocator() {
			SPADES_MARK_FUNCTION();

			VmaVulkanFunctions vkFuncs = {};
			vkFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
			vkFuncs.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

			// Use the extension enablement flags recorded by CreateLogicalDevice.
			VmaAllocatorCreateFlags allocatorFlags = 0;
			if (dedicatedAllocEnabled) allocatorFlags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
			if (bindMemory2Enabled)    allocatorFlags |= VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT;

			VmaAllocatorCreateInfo allocatorInfo = {};
			allocatorInfo.flags            = allocatorFlags;
			allocatorInfo.physicalDevice   = physicalDevice;
			allocatorInfo.device           = device;
			allocatorInfo.instance         = instance;
			allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;
			allocatorInfo.pVulkanFunctions = &vkFuncs;

			VkResult result = vmaCreateAllocator(&allocatorInfo, &allocator);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create VMA allocator (error code: %d)", result);
			}
			SPLog("VMA allocator created (flags: 0x%x)", allocatorFlags);
		}

		void SDLVulkanDevice::CreateSwapchain() {
			// Query swapchain support
			VkSurfaceCapabilitiesKHR capabilities;
			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

			uint32_t formatCount;
			vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
			std::vector<VkSurfaceFormatKHR> formats(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

			uint32_t presentModeCount;
			vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
			std::vector<VkPresentModeKHR> presentModes(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount,
				presentModes.data());

			// Choose surface format (prefer SRGB if available)
			VkSurfaceFormatKHR surfaceFormat = formats[0];
			for (const auto& availableFormat : formats) {
				if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
					availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
					surfaceFormat = availableFormat;
					break;
				}
			}

			// Choose present mode (prefer MAILBOX for lower latency, fall back to FIFO)
			VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
			for (const auto& availablePresentMode : presentModes) {
				if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
					presentMode = availablePresentMode;
					break;
				}
			}

			// Choose swap extent
			if (capabilities.currentExtent.width != UINT32_MAX) {
				swapchainExtent = capabilities.currentExtent;
			} else {
				swapchainExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
				swapchainExtent.width = std::max(capabilities.minImageExtent.width,
					std::min(capabilities.maxImageExtent.width, swapchainExtent.width));
				swapchainExtent.height = std::max(capabilities.minImageExtent.height,
					std::min(capabilities.maxImageExtent.height, swapchainExtent.height));
			}

			// Request one more image than the minimum to avoid waiting
			uint32_t imageCount = capabilities.minImageCount + 1;
			if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
				imageCount = capabilities.maxImageCount;
			}

			VkSwapchainCreateInfoKHR createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			createInfo.surface = surface;
			createInfo.minImageCount = imageCount;
			createInfo.imageFormat = surfaceFormat.format;
			createInfo.imageColorSpace = surfaceFormat.colorSpace;
			createInfo.imageExtent = swapchainExtent;
			createInfo.imageArrayLayers = 1;
			// TRANSFER_DST is required because the renderer presents by blitting
			// the final post-process image into the swapchain image
			// (vkCmdBlitImage in VulkanRenderer::Flip), not by rendering into it
			// directly. Without this bit that blit is undefined behaviour, which
			// validation reports as "dstImage ... requires
			// VK_IMAGE_USAGE_TRANSFER_DST_BIT".
			createInfo.imageUsage =
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

			// Not every surface allows the extra usage; fall back rather than
			// fail swapchain creation outright.
			if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
				SPLog("Warning: surface does not support TRANSFER_DST swapchain usage; "
				      "the present blit will be invalid on this driver");
				createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			}

			uint32_t queueFamilyIndices[] = {graphicsQueueFamily, presentQueueFamily};
			if (graphicsQueueFamily != presentQueueFamily) {
				createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				createInfo.queueFamilyIndexCount = 2;
				createInfo.pQueueFamilyIndices = queueFamilyIndices;
			} else {
				createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			}

			createInfo.preTransform = capabilities.currentTransform;
			createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			createInfo.presentMode = presentMode;
			createInfo.clipped = VK_TRUE;
			createInfo.oldSwapchain = VK_NULL_HANDLE;

			VkResult result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create swapchain (error code: %d)", result);
			}

			// Retrieve swapchain images
			vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
			swapchainImages.resize(imageCount);
			vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

			swapchainImageFormat = surfaceFormat.format;

			SPLog("Vulkan swapchain created (%ux%u, %u images)",
				swapchainExtent.width, swapchainExtent.height, imageCount);
		}

		void SDLVulkanDevice::CreateImageViews() {
			swapchainImageViews.resize(swapchainImages.size());

			for (size_t i = 0; i < swapchainImages.size(); i++) {
				VkImageViewCreateInfo createInfo{};
				createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				createInfo.image = swapchainImages[i];
				createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				createInfo.format = swapchainImageFormat;
				createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				createInfo.subresourceRange.baseMipLevel = 0;
				createInfo.subresourceRange.levelCount = 1;
				createInfo.subresourceRange.baseArrayLayer = 0;
				createInfo.subresourceRange.layerCount = 1;

				VkResult result = vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[i]);
				if (result != VK_SUCCESS) {
					SPRaise("Failed to create image view (error code: %d)", result);
				}
			}

			SPLog("Created %zu swapchain image views", swapchainImageViews.size());

			// One renderFinished semaphore per swapchain image (see CreateSyncObjects).
			// Tied to the swapchain lifecycle so a resize that changes the image count
			// rebuilds them correctly.
			renderFinishedSemaphores.resize(swapchainImages.size());
			VkSemaphoreCreateInfo semaphoreInfo{};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			for (size_t i = 0; i < renderFinishedSemaphores.size(); i++) {
				if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS) {
					SPRaise("Failed to create renderFinished semaphore");
				}
			}
		}

		void SDLVulkanDevice::CreateCommandPool() {
			VkCommandPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.queueFamilyIndex = graphicsQueueFamily;
			poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

			VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create command pool (error code: %d)", result);
			}

			SPLog("Vulkan command pool created");
		}

		void SDLVulkanDevice::CreateSyncObjects() {
			// imageAvailable semaphores and inFlightFences are per frame-in-flight.
			// renderFinished semaphores are per swapchain image instead (created in
			// CreateImageViews, destroyed in CleanupSwapchain): a binary semaphore
			// waited on by vkQueuePresentKHR must not be re-signalled until that
			// present's wait has drained, which is only guaranteed once the image is
			// re-acquired — so the semaphore must follow the image, not the frame.
			imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
			inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
			imagesInFlight.resize(swapchainImages.size(), VK_NULL_HANDLE);

			VkSemaphoreCreateInfo semaphoreInfo{};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

			for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
				if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
					vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
					SPRaise("Failed to create synchronization objects");
				}
			}

			SPLog("Vulkan synchronization objects created");
		}

		void SDLVulkanDevice::CleanupSwapchain() {
			// renderFinished semaphores are per swapchain image (see CreateSyncObjects).
			for (auto sem : renderFinishedSemaphores) {
				if (sem != VK_NULL_HANDLE)
					vkDestroySemaphore(device, sem, nullptr);
			}
			renderFinishedSemaphores.clear();

			for (auto imageView : swapchainImageViews) {
				vkDestroyImageView(device, imageView, nullptr);
			}
			swapchainImageViews.clear();

			if (swapchain != VK_NULL_HANDLE) {
				vkDestroySwapchainKHR(device, swapchain, nullptr);
				swapchain = VK_NULL_HANDLE;
			}
		}

		uint32_t SDLVulkanDevice::AcquireNextImage(VkSemaphore* outImageAvailableSemaphore, VkSemaphore* outRenderFinishedSemaphore) {
			// Note: Frame synchronization is handled by VulkanRenderer's fences in EndScene.
			// Removed redundant fence wait that was causing frame timing issues.

			uint32_t imageIndex;
			VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
				imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

			if (result == VK_ERROR_OUT_OF_DATE_KHR) {
				RecreateSwapchain();
				return UINT32_MAX; // Signal caller to retry
			} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
				SPRaise("Failed to acquire swapchain image");
			}

			// imageAvailable follows the frame-in-flight; renderFinished follows the
			// acquired swapchain image (waited on by the matching vkQueuePresentKHR).
			*outImageAvailableSemaphore = imageAvailableSemaphores[currentFrame];
			*outRenderFinishedSemaphore = renderFinishedSemaphores[imageIndex];
			return imageIndex;
		}

		void SDLVulkanDevice::PresentImage(uint32_t imageIndex, VkSemaphore* waitSemaphores,
										   uint32_t waitSemaphoreCount) {
			VkPresentInfoKHR presentInfo{};
			presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			presentInfo.waitSemaphoreCount = waitSemaphoreCount;
			presentInfo.pWaitSemaphores = waitSemaphores;

			VkSwapchainKHR swapchains[] = {swapchain};
			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = swapchains;
			presentInfo.pImageIndices = &imageIndex;

			VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);

			if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
				RecreateSwapchain();
			} else if (result != VK_SUCCESS) {
				SPRaise("Failed to present swapchain image");
			}

			currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
		}

		void SDLVulkanDevice::WaitForFences() {
			vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
			vkResetFences(device, 1, &inFlightFences[currentFrame]);
		}

		void SDLVulkanDevice::RecreateSwapchain() {
			int width = 0, height = 0;
			SDL_GetWindowSize(window, &width, &height);
			while (width == 0 || height == 0) {
				SDL_GetWindowSize(window, &width, &height);
				SDL_Delay(10);
			}

			vkDeviceWaitIdle(device);

			CleanupSwapchain();

			w = width;
			h = height;
			CreateSwapchain();
			CreateImageViews();

			swapchainGeneration++;
			SPLog("Swapchain recreated (%dx%d)", w, h);
		}

	} // namespace gui
} // namespace spades

#endif // USE_VULKAN
