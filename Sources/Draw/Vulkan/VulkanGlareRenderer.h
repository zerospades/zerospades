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

#include <vector>
#include <vulkan/vulkan.h>

#include <Client/IRenderer.h>
#include <Core/RefCountedObject.h>

#include "VulkanImage.h"

namespace spades {
	namespace draw {
		class VulkanRenderer;
		class VulkanImage;

		/**
		 * Draws the scene's glares (`client::GlareParam`) onto the swapchain image
		 * over the finished frame, before the 2D drawing, and under the first-person
		 * view's models, which the scene's depth tells apart.
		 */
		class VulkanGlareRenderer {
			struct Glare {
				Handle<VulkanImage> image;
				client::GlareParam param;
			};

			VulkanRenderer& renderer;
			VkDevice device;

			VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
			VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
			VkPipeline pipeline = VK_NULL_HANDLE;

			/** One pool per frame in flight, reset when its frame is recorded again. */
			std::vector<VkDescriptorPool> pools;

			std::vector<Glare> glares;

			void CreatePipeline(VkRenderPass swapchainRenderPass);

		public:
			/** `swapchainRenderPass` is the one the 2D drawing records into. */
			VulkanGlareRenderer(VulkanRenderer&, VkRenderPass swapchainRenderPass,
			                    std::size_t framesInFlight);
			~VulkanGlareRenderer();

			VulkanGlareRenderer(const VulkanGlareRenderer&) = delete;
			VulkanGlareRenderer& operator=(const VulkanGlareRenderer&) = delete;

			void Clear();
			void Add(VulkanImage&, const client::GlareParam&);

			/**
			 * Records the glares into the swapchain render pass under way, while the
			 * scene's resolved depth still holds the scene they were added to.
			 */
			void Render(VkCommandBuffer, std::size_t frameSlot);
		};
	} // namespace draw
} // namespace spades
