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

#include "VulkanImageManager.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include "VulkanImageWrapper.h"
#include "VulkanBuffer.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <cstring>

namespace spades {
	namespace draw {

		VulkanImageManager::VulkanImageManager(VulkanRenderer& r, Handle<gui::SDLVulkanDevice> dev)
		    : renderer(r), device(dev), whiteImage(nullptr) {
			SPADES_MARK_FUNCTION();
		}

		VulkanImageManager::~VulkanImageManager() {
			SPADES_MARK_FUNCTION();
			ClearCache();
		}

		Handle<client::IImage> VulkanImageManager::RegisterImage(const std::string& name) {
			SPADES_MARK_FUNCTION();

			auto it = images.find(name);
			if (it == images.end()) {
				Handle<client::IImage> img = CreateImage(name);
				if (img) {
					images[name] = img;
				}
				return img;
			}
			return it->second;
		}

		Handle<client::IImage> VulkanImageManager::GetWhiteImage() {
			if (!whiteImage) {
				whiteImage = RegisterImage("Gfx/White.tga");
			}
			return whiteImage;
		}

		Handle<client::IImage> VulkanImageManager::CreateImage(const std::string& name) {
			SPADES_MARK_FUNCTION();

			try {
				Handle<Bitmap> bmp = Bitmap::Load(name);
				if (!bmp) {
					SPLog("VulkanImageManager: Failed to load bitmap '%s'", name.c_str());
					return Handle<client::IImage>();
				}

				uint32_t width = static_cast<uint32_t>(bmp->GetWidth());
				uint32_t height = static_cast<uint32_t>(bmp->GetHeight());

				// Create Vulkan image
				Handle<VulkanImage> vkImage(new VulkanImage(
					device, width, height, VK_FORMAT_R8G8B8A8_UNORM,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), false);

				// Upload bitmap data to the image
				// Flip the bitmap vertically for Vulkan (bitmap is in OpenGL bottom-left format)
				VkDeviceSize imageSize = width * height * 4;
				std::vector<uint8_t> flippedData(imageSize);
				const uint8_t* srcPixels = reinterpret_cast<const uint8_t*>(bmp->GetPixels());
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
					SPRaise("Failed to allocate command buffer for image upload");
				}

				VkCommandBufferBeginInfo beginInfo{};
				beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
					SPRaise("Failed to begin command buffer for image upload");
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

				// Submit and wait on an explicit fence. A bare vkQueueWaitIdle with
				// unchecked results can silently leave the texture un-uploaded (blank)
				// on some drivers (observed on AMD/amdvlk), which is invisible for
				// additively-blended images like weapon reflex reticles. Check every
				// result and fail loudly with the image name so a bad upload is never
				// cached as a valid-but-blank texture.
				VkSubmitInfo submitInfo{};
				submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &commandBuffer;

				VkFenceCreateInfo fenceInfo{};
				fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
				VkFence uploadFence = VK_NULL_HANDLE;
				vkCreateFence(device->GetDevice(), &fenceInfo, nullptr, &uploadFence);

				VkResult submitRes =
				    vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, uploadFence);
				VkResult waitRes = VK_SUCCESS;
				if (submitRes == VK_SUCCESS) {
					waitRes = (uploadFence != VK_NULL_HANDLE)
					              ? vkWaitForFences(device->GetDevice(), 1, &uploadFence,
					                                VK_TRUE, UINT64_MAX)
					              : vkQueueWaitIdle(device->GetGraphicsQueue());
				}
				if (uploadFence != VK_NULL_HANDLE)
					vkDestroyFence(device->GetDevice(), uploadFence, nullptr);
				vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(), 1, &commandBuffer);

				if (submitRes != VK_SUCCESS)
					SPRaise("Failed to submit upload for image '%s' (error %d)", name.c_str(), submitRes);
				if (waitRes != VK_SUCCESS)
					SPRaise("Upload did not complete for image '%s' (error %d)", name.c_str(), waitRes);

				// Create sampler for the image
				vkImage->CreateSampler();

				SPLog("VulkanImageManager: uploaded '%s' (%ux%u)", name.c_str(), width, height);

				// Wrap in IImage interface
				return Handle<client::IImage>(
					new VulkanImageWrapper(vkImage, static_cast<float>(width), static_cast<float>(height)),
					false).Cast<client::IImage>();
			} catch (const std::exception& ex) {
				SPLog("Failed to create Vulkan image '%s': %s", name.c_str(), ex.what());
				return Handle<client::IImage>();
			}
		}

		void VulkanImageManager::ClearCache() {
			SPADES_MARK_FUNCTION();
			images.clear();
			whiteImage = Handle<client::IImage>();
		}

	} // namespace draw
} // namespace spades
