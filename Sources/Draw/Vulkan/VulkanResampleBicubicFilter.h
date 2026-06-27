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

#include <vector>
#include <vulkan/vulkan.h>
#include "VulkanPostProcessFilter.h"

namespace spades {
	namespace draw {

		// Bicubic upscale (Vulkan port of GLResampleBicubicFilter,
		// r_scaleFilter == 2). Single pass; output size may differ from
		// input size — the framebuffer/viewport follow `output`.

		class VulkanResampleBicubicFilter : public VulkanPostProcessFilter {

			VkFormat colorFormat;
			VkSampler linearSampler;

			VkRenderPass ppRenderPass;
			VkDescriptorSetLayout singleSamplerDSL;
			VkPipelineLayout resampleLayout;
			VkPipeline resamplePipeline;

			static constexpr int MAX_FRAME_SLOTS = 2;
			VkDescriptorPool perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];

			void InitRenderPass();
			void InitDescriptorSetLayout();
			void InitPipeline();
			void InitDescriptorPools();

			VkShaderModule LoadSPIRV(const char* path);
			VkFramebuffer MakeFramebuffer(VulkanImage* image, int frameSlot);
			VkDescriptorSet BindTexture(int frameSlot, VkImageView view);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanResampleBicubicFilter(VulkanRenderer& renderer);
			~VulkanResampleBicubicFilter();

			void Filter(VkCommandBuffer cmd, VulkanImage* input, VulkanImage* output) override;
		};

	} // namespace draw
} // namespace spades
