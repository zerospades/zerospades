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
#include <Client/IImage.h>
#include "VulkanPostProcessFilter.h"

namespace spades {
	namespace draw {
		class VulkanBuffer;

		// Bloom filter (r_bloom; mirrors GLLensDustFilter, the real GL
		// r_bloom — GLBloomFilter is dead code there).
		//
		// Algorithm:
		//   1. Downsample 6 levels via bilinear passthrough.
		//   2. Composite levels back from smallest to second-largest using
		//      mix(large, small, alpha) where alpha = sqrt(cnt/(cnt+1)).
		//   3. LensDust composite: scene * 0.95 + dust² * bloom * 2 + grain,
		//      with the dust overlay texture and a per-frame 128×128 noise
		//      texture for film grain, matching GL's LensDust.fs.
		//
		// Call Filter(cmd, input, output).  input must be in
		// SHADER_READ_ONLY_OPTIMAL; output ends up in SHADER_READ_ONLY_OPTIMAL.

		class VulkanBloomFilter : public VulkanPostProcessFilter {

			VkFormat colorFormat;
			VkSampler linearSampler;

			// Single render pass: DONT_CARE load, UNDEFINED → SHADER_READ_ONLY.
			// Used for all passes (downsample, upsample composite, final composite).
			VkRenderPass ppRenderPass;

			VkDescriptorSetLayout singleSamplerDSL;
			VkDescriptorSetLayout dualSamplerDSL;
			VkDescriptorSetLayout quadSamplerDSL;

			VkPipelineLayout downsampleLayout;  // singleSamplerDSL, no push constants
			VkPipelineLayout gaussLayout;       // singleSamplerDSL + push constant vec2 unitShift
			VkPipelineLayout upsampleLayout;    // dualSamplerDSL + push constant float alpha
			VkPipelineLayout compositeLayout;   // quadSamplerDSL + push constant vec4 noise factor

			VkPipeline downsamplePipeline;
			VkPipeline gaussPipeline;
			VkPipeline upsamplePipeline;
			VkPipeline compositePipeline;

			static constexpr int MAX_FRAME_SLOTS = 2;
			VkDescriptorPool perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];

			// LensDust composite inputs
			Handle<client::IImage> dustImage;      // Textures/LensDustTexture.jpg
			Handle<VulkanImage> noiseImage;        // 128×128 RGBA8, refreshed per frame
			Handle<VulkanBuffer> noiseStaging;
			std::vector<uint32_t> noiseData;

			void UpdateNoise(VkCommandBuffer cmd);

			void InitRenderPass();
			void InitDescriptorSetLayouts();
			void InitPipelines();
			void InitDescriptorPools();

			VkShaderModule LoadSPIRV(const char* path);

			VkFramebuffer MakeFramebuffer(VkRenderPass rp, VulkanImage* image, int frameSlot);

			VkDescriptorSet BindTexture(int frameSlot, VkDescriptorSetLayout dsl,
			                            VkImageView view);
			VkDescriptorSet BindTextures(int frameSlot, VkDescriptorSetLayout dsl,
			                             VkImageView view0, VkImageView view1);
			VkDescriptorSet BindTextures4(int frameSlot, const VkImageView* views,
			                              const VkSampler* samplers);

			void DrawFullscreen(VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb,
			                    uint32_t width, uint32_t height,
			                    VkPipeline pipeline, VkPipelineLayout layout,
			                    VkDescriptorSet ds);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanBloomFilter(VulkanRenderer& renderer);
			~VulkanBloomFilter();

			void Filter(VkCommandBuffer cmd,
			            VulkanImage* input, VulkanImage* output) override;
		};

	} // namespace draw
} // namespace spades
