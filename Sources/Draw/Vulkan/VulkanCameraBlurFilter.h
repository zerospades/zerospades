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
#include <Core/Math.h>
#include "VulkanPostProcessFilter.h"

namespace spades {
	namespace draw {

		// Camera motion blur (Vulkan port of GLCameraBlurFilter).
		//
		// Reprojects each pixel into last frame's view (rotation only) and
		// smears along the motion vector, iterating log5(movePixels) passes
		// with decreasing shutter scale. Also implements sceneDef.radialBlur.
		//
		// Call Apply(cmd, input, output, intensity, radialBlur); returns
		// false when the blur was skipped (no motion / camera cut), in which
		// case `output` was not written and the caller must not swap.

		class VulkanCameraBlurFilter : public VulkanPostProcessFilter {

			VkFormat colorFormat;
			VkSampler linearSampler;

			VkRenderPass ppRenderPass;
			VkDescriptorSetLayout dualSamplerDSL;
			VkPipelineLayout blurLayout;
			VkPipeline blurPipeline;

			static constexpr int MAX_FRAME_SLOTS = 2;
			VkDescriptorPool perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];

			Matrix4 prevMatrix;

			void InitRenderPass();
			void InitDescriptorSetLayout();
			void InitPipeline();
			void InitDescriptorPools();

			VkShaderModule LoadSPIRV(const char* path);
			VkFramebuffer MakeFramebuffer(VulkanImage* image, int frameSlot);
			VkDescriptorSet BindTextures(int frameSlot, VkImageView color, VkImageView depth,
			                             VkSampler depthSampler);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanCameraBlurFilter(VulkanRenderer& renderer);
			~VulkanCameraBlurFilter();

			bool Apply(VkCommandBuffer cmd, VulkanImage* input, VulkanImage* output,
			           float intensity, float radialBlur);

			void Filter(VkCommandBuffer cmd, VulkanImage* input, VulkanImage* output) override {
				Apply(cmd, input, output, 0.2f, 0.0f);
			}
		};

	} // namespace draw
} // namespace spades
