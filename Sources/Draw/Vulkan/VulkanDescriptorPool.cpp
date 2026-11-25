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

#include "VulkanDescriptorPool.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <vector>

namespace spades {
	namespace draw {

		VulkanDescriptorPool::VulkanDescriptorPool(Handle<gui::SDLVulkanDevice> dev, uint32_t sets)
		: device(std::move(dev)),
		  descriptorPool(VK_NULL_HANDLE),
		  maxSets(sets) {

			SPADES_MARK_FUNCTION();

			// Create descriptor pool with support for UBOs and combined image samplers
			std::vector<VkDescriptorPoolSize> poolSizes = {
				{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets * 10},
				{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 10}
			};

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
			poolInfo.pPoolSizes = poolSizes.data();
			poolInfo.maxSets = maxSets;
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

			VkResult result = vkCreateDescriptorPool(device->GetDevice(), &poolInfo, nullptr, &descriptorPool);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to create descriptor pool (error code: %d)", result);
			}

			SPLog("Created Vulkan descriptor pool (max sets: %u)", maxSets);
		}

		VulkanDescriptorPool::~VulkanDescriptorPool() {
			SPADES_MARK_FUNCTION();

			if (descriptorPool != VK_NULL_HANDLE && device) {
				vkDestroyDescriptorPool(device->GetDevice(), descriptorPool, nullptr);
			}
		}

		VkDescriptorSet VulkanDescriptorPool::AllocateDescriptorSet(VkDescriptorSetLayout layout) {
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &layout;

			VkDescriptorSet descriptorSet;
			VkResult result = vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descriptorSet);
			if (result != VK_SUCCESS) {
				SPRaise("Failed to allocate descriptor set (error code: %d)", result);
			}

			return descriptorSet;
		}

		void VulkanDescriptorPool::Reset() {
			// IMPORTANT: Caller must ensure device is idle or all command buffers
			// using descriptor sets from this pool have finished execution.
			// Vulkan spec 13.2.3 requires descriptor sets not be in use when pool is reset.
			// The caller should call vkDeviceWaitIdle() or use fences before calling this.
			vkResetDescriptorPool(device->GetDevice(), descriptorPool, 0);
			SPLog("Reset descriptor pool");
		}

	} // namespace draw
} // namespace spades
