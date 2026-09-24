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

#include "VulkanImageWrapper.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Bitmap.h>
#include <Core/Debug.h>
#include <cstring>

namespace spades {
	namespace draw {

		VulkanImageWrapper::VulkanImageWrapper(Handle<VulkanImage> img, float w, float h)
		    : image(std::move(img)), width(w), height(h) {
			SPADES_MARK_FUNCTION();
		}

		VulkanImageWrapper::~VulkanImageWrapper() {
			SPADES_MARK_FUNCTION();
		}

		void VulkanImageWrapper::Update(Bitmap& bmp, int x, int y) {
			SPADES_MARK_FUNCTION();

			if (!image) {
				SPLog("VulkanImageWrapper::Update: No image to update");
				return;
			}

			auto device = image->GetDevice();
			VkDevice vkDevice = device->GetDevice();

			uint32_t updateWidth = bmp.GetWidth();
			uint32_t updateHeight = bmp.GetHeight();
			VkDeviceSize imageSize = updateWidth * updateHeight * 4;

			// Flip the bitmap vertically for Vulkan (matching full image upload behavior)
			std::vector<uint8_t> flippedData(imageSize);
			const uint8_t* srcPixels = reinterpret_cast<const uint8_t*>(bmp.GetPixels());
			uint32_t rowSize = updateWidth * 4;

			for (uint32_t y = 0; y < updateHeight; y++) {
				const uint8_t* srcRow = srcPixels + y * rowSize;
				uint8_t* dstRow = flippedData.data() + (updateHeight - 1 - y) * rowSize;
				std::memcpy(dstRow, srcRow, rowSize);
			}

			// Create staging buffer
			Handle<VulkanBuffer> stagingBuffer(new VulkanBuffer(
				device, imageSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), false);

			// Copy flipped bitmap data to staging buffer
			stagingBuffer->UpdateData(flippedData.data(), imageSize);

			// Create temporary command buffer
			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = device->GetCommandPool();
			allocInfo.commandBufferCount = 1;

			VkCommandBuffer commandBuffer;
			if (vkAllocateCommandBuffers(vkDevice, &allocInfo, &commandBuffer) != VK_SUCCESS) {
				SPRaise("Failed to allocate command buffer for image mipmaps");
			}

			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
				SPRaise("Failed to begin command buffer for image mipmaps");
			}

			// Transition to transfer dst layout
			image->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

			// Copy buffer to image region
			VkBufferImageCopy region{};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			// Note: The atlas image was created with vertically flipped data (OpenGL->Vulkan conversion)
			// So we need to flip the Y coordinate: bottom-left origin (y) -> top-left origin
			int flippedY = static_cast<int>(height) - y - static_cast<int>(updateHeight);
			region.imageOffset = {x, flippedY, 0};
			region.imageExtent = {updateWidth, updateHeight, 1};

			vkCmdCopyBufferToImage(commandBuffer, stagingBuffer->GetBuffer(),
				image->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

			// Transition back to shader read layout
			image->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

			vkEndCommandBuffer(commandBuffer);

			// Submit and wait
			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
			vkQueueWaitIdle(device->GetGraphicsQueue());

			vkFreeCommandBuffers(vkDevice, device->GetCommandPool(), 1, &commandBuffer);
		}

	} // namespace draw
} // namespace spades
