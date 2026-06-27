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

#include "VulkanBuffer.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <cstring>

namespace spades {
	namespace draw {

		VulkanBuffer::VulkanBuffer(Handle<gui::SDLVulkanDevice> dev, VkDeviceSize bufferSize,
		                           VkBufferUsageFlags bufferUsage, VkMemoryPropertyFlags memProperties)
		: device(std::move(dev)),
		  buffer(VK_NULL_HANDLE),
		  allocation(VK_NULL_HANDLE),
		  size(bufferSize),
		  usage(bufferUsage),
		  properties(memProperties),
		  mappedData(nullptr) {

			SPADES_MARK_FUNCTION();

			// Create buffer
			VkBufferCreateInfo bufferInfo{};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.size = size;
			bufferInfo.usage = usage;
			bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo vmaAllocInfo = {};
			vmaAllocInfo.requiredFlags = memProperties;
			if (memProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
				vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			}

			VkResult result = vmaCreateBuffer(device->GetAllocator(), &bufferInfo, &vmaAllocInfo,
			                                  &buffer, &allocation, nullptr);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create Vulkan buffer (error code: %d)", result);
			}
		}

		VulkanBuffer::~VulkanBuffer() {
			SPADES_MARK_FUNCTION();

			if (mappedData) {
				Unmap();
			}

			if (buffer != VK_NULL_HANDLE) {
				vmaDestroyBuffer(device->GetAllocator(), buffer, allocation);
				buffer = VK_NULL_HANDLE;
				allocation = VK_NULL_HANDLE;
			}
		}

		void* VulkanBuffer::Map() {
			if (mappedData) {
				return mappedData;
			}

			VkResult result = vmaMapMemory(device->GetAllocator(), allocation, &mappedData);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to map buffer memory (error code: %d)", result);
			}

			return mappedData;
		}

		void VulkanBuffer::Unmap() {
			if (mappedData) {
				vmaUnmapMemory(device->GetAllocator(), allocation);
				mappedData = nullptr;
			}
		}

		void VulkanBuffer::UpdateData(const void* data, VkDeviceSize dataSize, VkDeviceSize offset) {
			if (offset + dataSize > size) {
				SPRaise("Buffer update exceeds buffer size");
			}

			void* mapped = Map();
			std::memcpy(static_cast<char*>(mapped) + offset, data, dataSize);

			// Flush memory if not coherent to make writes visible to GPU
			if (!(properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
				vmaFlushAllocation(device->GetAllocator(), allocation, offset, dataSize);
			}

			Unmap();
		}

		void VulkanBuffer::CopyFrom(VulkanBuffer& srcBuffer, VkCommandBuffer commandBuffer,
		                            VkDeviceSize srcOffset, VkDeviceSize dstOffset, VkDeviceSize copySize) {
			if (copySize == VK_WHOLE_SIZE) {
				copySize = srcBuffer.GetSize();
			}

			VkBufferCopy copyRegion{};
			copyRegion.srcOffset = srcOffset;
			copyRegion.dstOffset = dstOffset;
			copyRegion.size = copySize;

			vkCmdCopyBuffer(commandBuffer, srcBuffer.GetBuffer(), buffer, 1, &copyRegion);
		}

	} // namespace draw
} // namespace spades
