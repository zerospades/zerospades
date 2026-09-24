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

		// Depth of Field post-process filter.
		//
		// Algorithm (mirrors GLDepthOfFieldFilter):
		//   1. Generate per-pixel circle-of-confusion radius from the depth buffer.
		//   2. For low quality (r_depthOfField == 1): Gaussian-blur the CoC map and
		//      mix to propagate blur radii into adjacent pixels; reduce colour
		//      resolution if the image is large.
		//   3. Scatter-blur the colour image along three directions (60° apart) using
		//      the CoC as the tap spread.  A second round of blurs completes the
		//      hexagonal bokeh shape.
		//   4. Composite the blurred and sharp layers according to CoC.
		//
		// Standard quality uses DoFBlur (uniform-weight 8-tap scatter).
		// High quality (r_depthOfField >= 2) uses DoFBlur2 (CoC-weighted 8-tap).
		//
		// Descriptor set layout — bindings are pipeline-specific; see shader sources.
		// Depth image and all temporary images must be in SHADER_READ_ONLY_OPTIMAL
		// on entry.  Output ends up in SHADER_READ_ONLY_OPTIMAL.

		class VulkanDepthOfFieldFilter : public VulkanPostProcessFilter {

			VkFormat colorFormat;
			VkSampler linearSampler;

			// Two render passes: one for RGBA colour images, one for R8 CoC images.
			VkRenderPass colorRenderPass;
			VkRenderPass cocRenderPass;

			// Descriptor set layouts.
			VkDescriptorSetLayout singleSamplerDSL; // 1 combined sampler
			VkDescriptorSetLayout dualSamplerDSL;   // 2 combined samplers
			VkDescriptorSetLayout quadSamplerDSL;   // 4 combined samplers

			// Pipeline layouts.
			VkPipelineLayout cocGenLayout;   // singleSamplerDSL + 48-byte push constant
			VkPipelineLayout gauss1DLayout;  // singleSamplerDSL + 8-byte push constant (vec2)
			VkPipelineLayout dualLayout;     // dualSamplerDSL, no push constants
			VkPipelineLayout blurLayout;     // dualSamplerDSL + 8-byte push constant (vec2)
			VkPipelineLayout doFMixLayout;   // quadSamplerDSL + 4-byte push constant (int)

			// Pipelines.
			VkPipeline cocGenPipeline;   // CoCGen   → R8
			VkPipeline gauss1DPipeline;  // Gauss1D  → R8 (separable CoC blur)
			VkPipeline cocMixPipeline;   // CoCMix   → R8
			VkPipeline blurPipeline;     // DoFBlur  → RGBA (standard quality)
			VkPipeline blur2Pipeline;    // DoFBlur2 → RGBA (high quality)
			VkPipeline addMixPipeline;   // DoFAddMix → RGBA (average two colour bufs)
			VkPipeline doFMixPipeline;   // DoFMix   → RGBA (final composite)
			// Passthrough downsample pipeline (reuses PassThrough.vk.fs.spv)
			VkPipelineLayout passthroughLayout;
			VkPipeline passthroughPipeline;

			static constexpr int MAX_FRAME_SLOTS = 2;
			VkDescriptorPool perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];

			void InitRenderPasses();
			void InitDescriptorSetLayouts();
			void InitPipelines();
			void InitDescriptorPools();

			VkShaderModule LoadSPIRV(const char* path);

			VkFramebuffer MakeFramebuffer(VkRenderPass rp, VulkanImage* img, int frameSlot);

			// Allocate a descriptor set and write N combined-image-sampler bindings.
			// views and samplers must be arrays of length count.
			VkDescriptorSet BindImages(int frameSlot, VkDescriptorSetLayout dsl,
			                           const VkImageView* views, const VkSampler* samplers,
			                           uint32_t count);

			void DrawFullscreen(VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb,
			                    uint32_t w, uint32_t h,
			                    VkPipeline pipeline, VkPipelineLayout layout,
			                    VkDescriptorSet ds);

			// Separable Gaussian blur on a R8 CoC image.
			// Returns a new CoC image; caller must add to deferred.
			Handle<VulkanImage> BlurCoC(VkCommandBuffer cmd, VulkanImage* coc,
			                            float spread, int frameSlot,
			                            std::vector<Handle<VulkanImage>>& deferred);

			// Scatter-blur along one direction.
			// offsetPixX/Y: offset in pixel space at full-image resolution / divide.
			// Returns a new colour image; caller must add inputs to deferred when done.
			Handle<VulkanImage> DoBlur(VkCommandBuffer cmd, VulkanImage* buffer,
			                           VulkanImage* coc,
			                           float offsetPixX, float offsetPixY,
			                           int frameSlot, bool highQuality,
			                           std::vector<Handle<VulkanImage>>& deferred);

			bool HighQuality() const;

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanDepthOfFieldFilter(VulkanRenderer& renderer);
			~VulkanDepthOfFieldFilter();

			// Reads DoF parameters from renderer.GetSceneDef() and r_depthOfFieldMaxCoc.
			void Filter(VkCommandBuffer cmd, VulkanImage* input, VulkanImage* output) override;
		};

	} // namespace draw
} // namespace spades
