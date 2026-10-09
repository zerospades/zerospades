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
#include <Core/Math.h>

namespace spades {
	namespace draw {

		// Color correction filter (Vulkan port of GLColorCorrectionFilter).
		//
		// A fullscreen pass that applies:
		//   - sharpening (r_sharpen), reversing a blur whose horizontal half is
		//     a pass of its own, as GL's
		//   - white-balance tint (cancels fog colour cast)
		//   - saturation desaturation toward gray
		//   - ACES filmic tonemap (the actual HDR compression)
		//   - smoothstep enhancement
		//
		// Tint and saturation are recomputed every frame from the fog colour,
		// eased towards the scene's as GL eases it, and the r_saturation cvar,
		// matching GLRenderer's.
		//
		// Call Filter(cmd, input, output). input must be in
		// SHADER_READ_ONLY_OPTIMAL; output ends up in SHADER_READ_ONLY_OPTIMAL.

		class VulkanColorCorrectionFilter : public VulkanPostProcessFilter {

			VkFormat colorFormat;
			VkSampler linearSampler;

			VkRenderPass ppRenderPass;
			VkDescriptorSetLayout singleSamplerDSL; // the blur's input
			VkDescriptorSetLayout dualSamplerDSL;   // the input and its blur
			VkPipelineLayout layout;
			VkPipeline pipeline;
			VkPipelineLayout blurLayout;
			VkPipeline blurPipeline;

			// The fog colour the tint is worked out from, eased towards the scene's a
			// little every frame as GL does; negative until the first frame
			Vector3 smoothedFogColor{-1.0F, -1.0F, -1.0F};

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
			VkDescriptorSet BindTextures(int frameSlot, VkImageView view, VkImageView blurred);

			/** A full-screen pipeline drawing `fragmentShader` with `pipelineLayout` */
			VkPipeline BuildPipeline(const char* fragmentShader, VkPipelineLayout pipelineLayout);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanColorCorrectionFilter(VulkanRenderer& renderer);
			~VulkanColorCorrectionFilter();

			void Filter(VkCommandBuffer cmd,
			            VulkanImage* input, VulkanImage* output) override;
		};

	} // namespace draw
} // namespace spades
