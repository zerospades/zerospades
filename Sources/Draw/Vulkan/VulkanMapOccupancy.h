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

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

#include <Core/RefCountedObject.h>
#include <Draw/Vulkan/vk_mem_alloc.h>

namespace spades {
	namespace client {
		class GameMap;
		class MapClearance;
	} // namespace client
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanBuffer;
		class VulkanRenderer;

		/**
		 * The map's `client::MapClearance` as a 3D image the dynamic lights' shaders
		 * walk rays through (`SceneLight/Lights.glsl`): an unsigned integer per
		 * block, x by y by z. It is uploaded whole when the map is set, then a few
		 * changed regions a frame through each frame in flight's own staging buffer.
		 */
		class VulkanMapOccupancy {
		public:
			/** The most changed regions a frame uploads; the rest wait for the next. */
			static constexpr std::uint32_t MaxRegionsPerFrame = 64;

			/** The stages that read the image: the lit fragments, the vertices of
			 * sprites lit at their corners, and the occlusion maps' tracing */
			static constexpr VkPipelineStageFlags ReaderStages =
			  VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			/** Keeps `map`'s clearance, with a staging buffer per frame in flight. */
			VulkanMapOccupancy(VulkanRenderer&, client::GameMap& map, std::size_t framesInFlight);

			/** A single block of no clearance, for the lights' set to bind without a
			 * map; the shaders don't walk it. */
			explicit VulkanMapOccupancy(VulkanRenderer&);

			~VulkanMapOccupancy();

			VulkanMapOccupancy(const VulkanMapOccupancy&) = delete;
			VulkanMapOccupancy& operator=(const VulkanMapOccupancy&) = delete;

			void GameMapChanged(int x, int y, int z);

			/** Records the uploads of regions changed since the last call into
			 * `commandBuffer`, outside of a render pass, through frame slot
			 * `frameSlot`'s staging buffer. */
			void Update(VkCommandBuffer commandBuffer, std::size_t frameSlot);

			VkImageView GetImageView() const { return imageView; }
			VkSampler GetSampler() const { return sampler; }

		private:
			Handle<gui::SDLVulkanDevice> device;
			Handle<client::GameMap> map;
			std::unique_ptr<client::MapClearance> clearance;

			VkImage image = VK_NULL_HANDLE;
			VmaAllocation allocation = VK_NULL_HANDLE;
			VkImageView imageView = VK_NULL_HANDLE;
			VkSampler sampler = VK_NULL_HANDLE;

			std::vector<Handle<VulkanBuffer>> staging;

			void CreateImage(std::uint32_t width, std::uint32_t height, std::uint32_t depth);

			/** Records `record` into a command buffer of its own and waits for the GPU
			 * to run it. */
			template <class F> void Submit(F&& record);
		};
	} // namespace draw
} // namespace spades
