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

		class VulkanBuffer : public RefCountedObject {
			Handle<gui::SDLVulkanDevice> device;
			VkBuffer buffer;
			VmaAllocation allocation;
			VkDeviceSize size;
			VkBufferUsageFlags usage;
			VkMemoryPropertyFlags properties;

			void* mappedData;

		protected:
			~VulkanBuffer();

		public:
			VulkanBuffer(Handle<gui::SDLVulkanDevice> device, VkDeviceSize size,
			             VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);

			VkBuffer GetBuffer() const { return buffer; }
			VkDeviceSize GetSize() const { return size; }

			// Map/unmap for host-visible buffers
			void* Map();
			void Unmap();

			// Copy data to buffer (for host-visible buffers)
			void UpdateData(const void* data, VkDeviceSize dataSize, VkDeviceSize offset = 0);

			// Copy from another buffer using command buffer
			void CopyFrom(VulkanBuffer& srcBuffer, VkCommandBuffer commandBuffer, VkDeviceSize srcOffset = 0,
			              VkDeviceSize dstOffset = 0, VkDeviceSize copySize = VK_WHOLE_SIZE);
		};

	} // namespace draw
} // namespace spades
