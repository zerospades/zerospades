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

#include "VulkanMapOccupancy.h"

#include <cstring>

#include "VulkanBuffer.h"
#include "VulkanRenderer.h"
#include <Client/GameMap.h>
#include <Client/MapClearance.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Gui/SDLVulkanDevice.h>

namespace spades {
	namespace draw {
		namespace {
			constexpr VkFormat kFormat = VK_FORMAT_R8_UINT;
			constexpr VkImageSubresourceRange kWholeImage = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
			                                                 1};

			/** Moves the image from `from` to `to` for the access and stages given. */
			void Transition(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout from,
			                VkImageLayout to, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
			                VkPipelineStageFlags srcStages, VkPipelineStageFlags dstStages) {
				VkImageMemoryBarrier barrier{};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.oldLayout = from;
				barrier.newLayout = to;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = image;
				barrier.subresourceRange = kWholeImage;
				barrier.srcAccessMask = srcAccess;
				barrier.dstAccessMask = dstAccess;
				vkCmdPipelineBarrier(commandBuffer, srcStages, dstStages, 0, 0, nullptr, 0, nullptr,
				                     1, &barrier);
			}
		} // namespace

		VulkanMapOccupancy::VulkanMapOccupancy(VulkanRenderer& renderer, client::GameMap& map,
		                                       std::size_t framesInFlight)
		    : device(renderer.GetDevice()), map(map) {
			SPADES_MARK_FUNCTION();

			clearance.reset(new client::MapClearance(map));
			const IntVector3 size = clearance->GetSize();
			CreateImage((std::uint32_t)size.x, (std::uint32_t)size.y, (std::uint32_t)size.z);

			// The whole map at once, through a staging buffer of its size
			std::vector<std::uint8_t> values;
			clearance->ComputeAll(values);
			Handle<VulkanBuffer> upload = Handle<VulkanBuffer>::New(
			  device, values.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			upload->UpdateData(values.data(), values.size());

			Submit([&](VkCommandBuffer commandBuffer) {
				Transition(commandBuffer, image, VK_IMAGE_LAYOUT_UNDEFINED,
				           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
				           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
				VkBufferImageCopy region{};
				region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
				region.imageExtent = {(std::uint32_t)size.x, (std::uint32_t)size.y,
				                      (std::uint32_t)size.z};
				vkCmdCopyBufferToImage(commandBuffer, upload->GetBuffer(), image,
				                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
				Transition(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
				           VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, ReaderStages);
			});

			// Changed regions are copied in from here, a frame in flight each.
			const VkDeviceSize stagingSize =
			  (VkDeviceSize)MaxRegionsPerFrame * client::MapClearance::RegionVolume;
			for (std::size_t i = 0; i < framesInFlight; i++) {
				Handle<VulkanBuffer> buffer = Handle<VulkanBuffer>::New(
				  device, stagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				buffer->Map();
				staging.push_back(std::move(buffer));
			}
		}

		VulkanMapOccupancy::VulkanMapOccupancy(VulkanRenderer& renderer)
		    : device(renderer.GetDevice()) {
			SPADES_MARK_FUNCTION();

			CreateImage(1, 1, 1);
			Submit([&](VkCommandBuffer commandBuffer) {
				Transition(commandBuffer, image, VK_IMAGE_LAYOUT_UNDEFINED,
				           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
				           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
				const VkClearColorValue none{};
				vkCmdClearColorImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				                     &none, 1, &kWholeImage);
				Transition(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
				           VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, ReaderStages);
			});
		}

		VulkanMapOccupancy::~VulkanMapOccupancy() {
			SPADES_MARK_FUNCTION();

			// Frames reading the image may still be in flight.
			VkDevice vkDevice = device->GetDevice();
			vkDeviceWaitIdle(vkDevice);
			if (sampler != VK_NULL_HANDLE)
				vkDestroySampler(vkDevice, sampler, nullptr);
			if (imageView != VK_NULL_HANDLE)
				vkDestroyImageView(vkDevice, imageView, nullptr);
			if (image != VK_NULL_HANDLE)
				vmaDestroyImage(device->GetAllocator(), image, allocation);
		}

		void VulkanMapOccupancy::CreateImage(std::uint32_t width, std::uint32_t height,
		                                     std::uint32_t depth) {
			VkImageCreateInfo imageInfo{};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_3D;
			imageInfo.format = kFormat;
			imageInfo.extent = {width, height, depth};
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
			if (vmaCreateImage(device->GetAllocator(), &imageInfo, &allocInfo, &image, &allocation,
			                   nullptr) != VK_SUCCESS)
				SPRaise("Failed to create the map occupancy image");

			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
			viewInfo.format = kFormat;
			viewInfo.subresourceRange = kWholeImage;
			if (vkCreateImageView(device->GetDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS)
				SPRaise("Failed to create the map occupancy image view");

			// Read by `texelFetch` alone; integer images can't be filtered anyway.
			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = VK_FILTER_NEAREST;
			samplerInfo.minFilter = VK_FILTER_NEAREST;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			if (vkCreateSampler(device->GetDevice(), &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
				SPRaise("Failed to create the map occupancy sampler");
		}

		template <class F> void VulkanMapOccupancy::Submit(F&& record) {
			VkDevice vkDevice = device->GetDevice();

			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = device->GetCommandPool();
			allocInfo.commandBufferCount = 1;
			VkCommandBuffer commandBuffer;
			if (vkAllocateCommandBuffers(vkDevice, &allocInfo, &commandBuffer) != VK_SUCCESS)
				SPRaise("Failed to allocate the map occupancy upload command buffer");

			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(commandBuffer, &beginInfo);
			record(commandBuffer);
			vkEndCommandBuffer(commandBuffer);

			VkFenceCreateInfo fenceInfo{};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			VkFence fence;
			if (vkCreateFence(vkDevice, &fenceInfo, nullptr, &fence) != VK_SUCCESS)
				SPRaise("Failed to create the map occupancy upload fence");

			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;
			const VkResult submitted =
			  vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, fence);
			const VkResult waited = submitted == VK_SUCCESS
			                          ? vkWaitForFences(vkDevice, 1, &fence, VK_TRUE, UINT64_MAX)
			                          : submitted;
			vkDestroyFence(vkDevice, fence, nullptr);
			vkFreeCommandBuffers(vkDevice, device->GetCommandPool(), 1, &commandBuffer);
			if (waited != VK_SUCCESS)
				SPRaise("Failed to upload the map occupancy (error code: %d)", waited);
		}

		void VulkanMapOccupancy::GameMapChanged(int x, int y, int z) {
			if (clearance)
				clearance->GameMapChanged(x, y, z);
		}

		void VulkanMapOccupancy::Update(VkCommandBuffer commandBuffer, std::size_t frameSlot) {
			SPADES_MARK_FUNCTION();

			if (!clearance || !clearance->HasChangedRegions())
				return;
			SPAssert(frameSlot < staging.size());

			VulkanBuffer& buffer = *staging[frameSlot];
			auto* out = static_cast<std::uint8_t*>(buffer.Map());

			std::vector<VkBufferImageCopy> copies;
			copies.reserve(MaxRegionsPerFrame);
			const std::uint32_t edge = client::MapClearance::RegionSize;
			clearance->TakeChangedRegions(
			  [&](const IntVector3& origin, const std::uint8_t* values) {
				  if (copies.size() == MaxRegionsPerFrame)
					  return false;

				  const VkDeviceSize offset =
				    (VkDeviceSize)copies.size() * client::MapClearance::RegionVolume;
				  std::memcpy(out + offset, values, client::MapClearance::RegionVolume);

				  VkBufferImageCopy copy{};
				  copy.bufferOffset = offset;
				  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
				  copy.imageOffset = {origin.x, origin.y, origin.z};
				  copy.imageExtent = {edge, edge, edge};
				  copies.push_back(copy);
				  return true;
			  });
			if (copies.empty())
				return;

			Transition(commandBuffer, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
			           VK_ACCESS_TRANSFER_WRITE_BIT, ReaderStages, VK_PIPELINE_STAGE_TRANSFER_BIT);
			vkCmdCopyBufferToImage(commandBuffer, buffer.GetBuffer(), image,
			                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                       (std::uint32_t)copies.size(), copies.data());
			Transition(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
			           VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, ReaderStages);
		}
	} // namespace draw
} // namespace spades
