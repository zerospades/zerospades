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
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanRenderer;
		class VulkanImage;

		// Base class for post-processing filters
		class VulkanPostProcessFilter {
		protected:
			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;

			VkPipeline pipeline;
			VkPipelineLayout pipelineLayout;
			VkDescriptorSetLayout descriptorSetLayout;
			VkRenderPass renderPass;

			virtual void CreatePipeline() = 0;
			virtual void CreateRenderPass() = 0;
			void DestroyResources();

		public:
			VulkanPostProcessFilter(VulkanRenderer&);
			virtual ~VulkanPostProcessFilter();

			virtual void Filter(VkCommandBuffer commandBuffer, VulkanImage* input, VulkanImage* output) = 0;
		};
	}
}
