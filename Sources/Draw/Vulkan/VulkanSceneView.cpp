/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "VulkanSceneView.h"

#include <cstring>

#include "VulkanBuffer.h"
#include "VulkanRenderer.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Gui/SDLVulkanDevice.h>

namespace spades {
	namespace draw {
		namespace {
			/** `SceneView` of `SceneView.glsl`, std140 */
			struct GpuView {
				float projectionView[16];
				float view[16];
				float eyeFogDistance[4];
				float fogColorMirrorClipZ[4];
				float sunDirection[4];
			};
			static_assert(sizeof(GpuView) == 176, "GpuView must match SceneView");

			void Store(float (&out)[4], const Vector3& v, float w) {
				out[0] = v.x;
				out[1] = v.y;
				out[2] = v.z;
				out[3] = w;
			}
		} // namespace

		VulkanSceneView::VulkanSceneView(VulkanRenderer& renderer, std::size_t framesInFlight)
		    : device(renderer.GetDevice()->GetDevice()) {
			SPADES_MARK_FUNCTION();

			const VkDescriptorSetLayoutBinding binding{
			  0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
			  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = 1;
			layoutInfo.pBindings = &binding;
			if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout) != VK_SUCCESS)
				SPRaise("Failed to create the scene view descriptor set layout");

			const std::uint32_t slotCount = static_cast<std::uint32_t>(framesInFlight);
			const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			                                    slotCount};
			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.maxSets = slotCount;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &poolSize;
			if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
				SPRaise("Failed to create the scene view descriptor pool");

			// Each view starts where a dynamic offset may.
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(renderer.GetDevice()->GetPhysicalDevice(), &properties);
			const VkDeviceSize alignment =
			  std::max<VkDeviceSize>(properties.limits.minUniformBufferOffsetAlignment, 1);
			stride = (sizeof(GpuView) + alignment - 1) / alignment * alignment;

			slots.resize(framesInFlight);
			for (Slot& slot : slots) {
				slot.buffer = Handle<VulkanBuffer>::New(
				  renderer.GetDevice(), stride * ViewCount, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				std::memset(slot.buffer->Map(), 0, stride * ViewCount);

				VkDescriptorSetAllocateInfo allocInfo{};
				allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				allocInfo.descriptorPool = pool;
				allocInfo.descriptorSetCount = 1;
				allocInfo.pSetLayouts = &setLayout;
				if (vkAllocateDescriptorSets(device, &allocInfo, &slot.set) != VK_SUCCESS)
					SPRaise("Failed to allocate a scene view descriptor set");

				const VkDescriptorBufferInfo bufferInfo{slot.buffer->GetBuffer(), 0,
				                                        sizeof(GpuView)};
				VkWriteDescriptorSet write{};
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.dstSet = slot.set;
				write.dstBinding = 0;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				write.pBufferInfo = &bufferInfo;
				vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
			}
		}

		VulkanSceneView::~VulkanSceneView() {
			SPADES_MARK_FUNCTION();
			// The sets go with their pool.
			if (pool != VK_NULL_HANDLE)
				vkDestroyDescriptorPool(device, pool, nullptr);
			if (setLayout != VK_NULL_HANDLE)
				vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
		}

		void VulkanSceneView::Write(std::size_t frameSlot, View view, const Parameters& parameters) {
			SPAssert(frameSlot < slots.size());

			GpuView out{};
			std::memcpy(out.projectionView, parameters.projectionView.m, sizeof(out.projectionView));
			std::memcpy(out.view, parameters.view.m, sizeof(out.view));
			Store(out.eyeFogDistance, parameters.eye, parameters.fogDistance);
			Store(out.fogColorMirrorClipZ, parameters.fogColor, parameters.mirrorClipZ);
			Store(out.sunDirection, parameters.sunDirection, 0.0F);

			auto* mapped = static_cast<std::uint8_t*>(slots[frameSlot].buffer->Map());
			std::memcpy(mapped + stride * static_cast<std::uint32_t>(view), &out, sizeof(out));
		}

		void VulkanSceneView::Bind(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
		                           VkPipelineBindPoint bindPoint, std::size_t frameSlot,
		                           View view) const {
			SPAssert(frameSlot < slots.size());
			const std::uint32_t offset =
			  static_cast<std::uint32_t>(stride * static_cast<std::uint32_t>(view));
			vkCmdBindDescriptorSets(commandBuffer, bindPoint, layout, Set, 1,
			                        &slots[frameSlot].set, 1, &offset);
		}
	} // namespace draw
} // namespace spades
