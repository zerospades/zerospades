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

#include <vulkan/vulkan.h>
#include <Draw/Vulkan/vk_mem_alloc.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {

		class VulkanImage : public RefCountedObject {
			Handle<gui::SDLVulkanDevice> device;
			VkImage image;
			VmaAllocation allocation;
			VkImageView imageView;
			VkSampler sampler;

			uint32_t width;
			uint32_t height;
			uint32_t arrayLayers;
			uint32_t mipLevels;
			VkFormat format;
			VkSampleCountFlagBits samples;
			VkImageLayout currentLayout;

			bool ownsImage; // If false, image is owned externally (e.g., swapchain)

		protected:
			~VulkanImage();

		public:
			// Create image with memory allocation. `samples` > 1 makes the image
			// multisampled (MSAA render target); such images can only be used as
			// attachments and read via texelFetch on a sampler2DMS — they cannot be
			// linearly sampled, copied, or blitted.
			VulkanImage(Handle<gui::SDLVulkanDevice> device, uint32_t width, uint32_t height,
			            VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
			            VkMemoryPropertyFlags properties,
			            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

			// Create 2D array image with memory allocation
			VulkanImage(Handle<gui::SDLVulkanDevice> device, uint32_t width, uint32_t height,
			            uint32_t arrayLayers, uint32_t mipLevels, VkFormat format,
			            VkImageTiling tiling, VkImageUsageFlags usage,
			            VkMemoryPropertyFlags properties);

			// Wrap existing image (e.g., from swapchain)
			VulkanImage(Handle<gui::SDLVulkanDevice> device, VkImage existingImage,
			            uint32_t width, uint32_t height, VkFormat format);

			VkImage GetImage() const { return image; }
			VkImageView GetImageView() const { return imageView; }
			VkSampler GetSampler() const { return sampler; }
			uint32_t GetWidth() const { return width; }
			uint32_t GetHeight() const { return height; }
			VkFormat GetFormat() const { return format; }
			VkSampleCountFlagBits GetSampleCount() const { return samples; }
			VkImageLayout GetCurrentLayout() const { return currentLayout; }
			Handle<gui::SDLVulkanDevice> GetDevice() const { return device; }
			uint32_t GetArrayLayers() const { return arrayLayers; }
			uint32_t GetMipLevels() const { return mipLevels; }

			// Transition image layout
			void TransitionLayout(VkCommandBuffer commandBuffer, VkImageLayout newLayout,
			                      VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
			                      VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask);

			// Copy data from buffer to image
			void CopyFromBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer);

			// Copy data from buffer to specific array layer
			void CopyFromBufferToLayer(VkCommandBuffer commandBuffer, VkBuffer buffer, uint32_t layer);

			// Copy region from buffer to image
			void CopyRegionFromBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer,
			                          uint32_t x, uint32_t y, uint32_t regionWidth, uint32_t regionHeight);

			// Generate mipmaps
			void GenerateMipmaps(VkCommandBuffer commandBuffer);

			// Create image view (called automatically in constructor)
			void CreateImageView(VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT);

			// Create sampler (optional, for texture sampling)
			void CreateSampler(VkFilter magFilter = VK_FILTER_LINEAR,
			                   VkFilter minFilter = VK_FILTER_LINEAR,
			                   VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			                   bool enableAnisotropy = true);
		};

	} // namespace draw
} // namespace spades
