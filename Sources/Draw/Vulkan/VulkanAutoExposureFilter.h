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
#include <Draw/Vulkan/vk_mem_alloc.h>
#include "VulkanPostProcessFilter.h"

namespace spades {
	namespace draw {

		// Auto-exposure filter.
		//
		// Algorithm (mirrors GLAutoExposureFilter):
		//   1. Preprocess pass  — extract peak brightness from the scene image.
		//   2. Downsample loop  — halve resolution iteratively down to 1×1.
		//   3. Compute-gain pass — blend the 1×1 brightness into a persistent
		//      exposure accumulator using temporal smoothing (SRC_ALPHA blend).
		//   4. Apply pass       — multiply the scene by the accumulated gain.
		//
		// Call Filter(cmd, input, output, dt).  input must be in
		// SHADER_READ_ONLY_OPTIMAL; output ends up in SHADER_READ_ONLY_OPTIMAL.

		class VulkanAutoExposureFilter : public VulkanPostProcessFilter {

			// Colour format used for all intermediate and persistent images.
			VkFormat colorFormat;

			// ---------- Persistent 1×1 exposure accumulator ----------
			VkImage        exposureImage;
			VmaAllocation  exposureAlloc;
			VkImageView    exposureImageView;
			VkFramebuffer  exposureFramebuffer;

			// Sampler used for all texture reads in this filter.
			VkSampler linearSampler;

			// ---------- Render passes ----------
			// ppRenderPass: DONT_CARE load, UNDEFINED→SHADER_READ_ONLY.
			//   Used for preprocess, downsample, and apply passes.
			VkRenderPass ppRenderPass;

			// gainFirstRenderPass: CLEAR load (clears to 0), UNDEFINED→SHADER_READ_ONLY.
			//   Used only on the very first frame to initialise the accumulator.
			VkRenderPass gainFirstRenderPass;

			// gainRenderPass: LOAD load, COLOR_ATTACHMENT→SHADER_READ_ONLY.
			//   Used every subsequent frame; blending accumulates the new gain.
			VkRenderPass gainRenderPass;

			// ---------- Descriptor set layouts ----------
			VkDescriptorSetLayout singleSamplerDSL; // binding 0: combined sampler
			VkDescriptorSetLayout dualSamplerDSL;   // binding 0+1: two combined samplers

			// ---------- Pipeline layouts ----------
			VkPipelineLayout preprocessLayout;   // singleSamplerDSL, no push constants
			VkPipelineLayout computeGainLayout;  // singleSamplerDSL + push constants
			VkPipelineLayout applyLayout;        // dualSamplerDSL, no push constants

			// ---------- Pipelines ----------
			VkPipeline preprocessPipeline;  // brightness extraction
			VkPipeline downsamplePipeline;  // passthrough (bilinear downsample)
			VkPipeline computeGainPipeline; // gain with SRC_ALPHA blend
			VkPipeline applyPipeline;       // multiply scene by gain

			// ---------- Per-frame-slot resources ----------
			// Descriptor sets and dynamic framebuffers are created each frame and
			// must not be freed until the GPU has finished with them.  We keep two
			// slots (matching maxFramesInFlight=2) and recycle them on next use.
			static constexpr int MAX_FRAME_SLOTS = 2;
			VkDescriptorPool perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];

			// Whether the exposure accumulator has been initialised.
			bool exposureInitialized;

			// ---------- Helpers ----------
			void InitRenderPasses();
			void InitDescriptorSetLayouts();
			void InitPipelines();
			void InitDescriptorPools();
			void InitExposureImage();

			VkShaderModule LoadSPIRV(const char* path, VkShaderStageFlagBits stage);

			// Creates a framebuffer for `image` in `renderPass` and registers it
			// for deferred destruction in the given frame slot.
			VkFramebuffer MakeFramebuffer(VkRenderPass rp, VulkanImage* image,
			                              int frameSlot);

			// Allocates a descriptor set from the per-frame pool and writes a
			// single combined-image-sampler at binding 0.
			VkDescriptorSet BindTexture(int frameSlot, VkDescriptorSetLayout dsl,
			                            VkImageView view);

			// Allocates a descriptor set with two combined-image-samplers.
			VkDescriptorSet BindTextures(int frameSlot, VkDescriptorSetLayout dsl,
			                             VkImageView view0, VkImageView view1);

			// Records a single fullscreen draw into `fb` using `pipeline`.
			void DrawFullscreen(VkCommandBuffer cmd, VkRenderPass rp,
			                    VkFramebuffer fb, uint32_t width, uint32_t height,
			                    VkPipeline pipeline, VkPipelineLayout layout,
			                    VkDescriptorSet ds);

			// Pure-virtual stubs — this filter manages its own pipelines.
			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanAutoExposureFilter(VulkanRenderer& renderer);
			~VulkanAutoExposureFilter();

			// VulkanPostProcessFilter interface (unused externally; use the dt overload).
			void Filter(VkCommandBuffer cmd,
			            VulkanImage* input, VulkanImage* output) override {
				Filter(cmd, input, output, 0.016f);
			}

			// Main entry point called by the post-process chain.
			void Filter(VkCommandBuffer cmd,
			            VulkanImage* input, VulkanImage* output, float dt);
		};

	} // namespace draw
} // namespace spades
