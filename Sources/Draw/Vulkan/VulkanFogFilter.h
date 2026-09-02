/*
 Copyright (c) 2021 Fran6nd

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

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>
#include "VulkanPostProcessFilter.h"

namespace spades {
	namespace draw {

		// Fog filter.
		//
		// Two variants are built; r_fogShadow picks at draw time:
		//   r_fogShadow == 1 → Fog  (Fog.vk.*  — DDA-style shadow raymarch, sharper,
		//                            no AO/radiosity; ports GLFogFilter)
		//   r_fogShadow == 2 → Fog2 (Fog2.vk.* — fixed 16-step march, smoother,
		//                            matches GLFogFilter2 minus the 3D AO/radiosity
		//                            textures that the Vulkan back-end does not yet
		//                            have)
		//
		// Descriptor bindings (set 0):
		//   Fog1: 0=color, 1=depth, 2=shadowMap (fine), 3=coarseShadowMap (8×8
		//         min/max companion, used by the coarse+fine traversal)
		//   Fog2: 0=color, 1=depth, 2=shadowMap, 3=ambientShadow3D, 4..7=radiosity{,X,Y,Z}
		//
		// Push-constant blocks differ between the two variants, so each pipeline
		// gets its own VkPipelineLayout.
		//
		// Call Filter(cmd, input, output).  input must be in SHADER_READ_ONLY_OPTIMAL;
		// output ends up in SHADER_READ_ONLY_OPTIMAL.  The depth image and the shadow
		// map image must also be in SHADER_READ_ONLY_OPTIMAL on entry.

		class VulkanFogFilter : public VulkanPostProcessFilter {

			VkFormat colorFormat;
			VkSampler colorSampler; // linear, clamp-to-edge

			VkRenderPass ppRenderPass;

			VkDescriptorSetLayout triSamplerDSL; // bindings 0..3 (Fog1, +coarse shadow)
			VkDescriptorSetLayout fog2DSL;       // bindings 0..7 (Fog2: +AO+4 radiosity)

			// Fog2 (default, r_fogShadow == 2)
			VkPipelineLayout fogLayout;
			VkPipeline       fogPipeline;

			// Fog / Fog1 (r_fogShadow == 1)
			VkPipelineLayout fogClassicLayout;
			VkPipeline       fogClassicPipeline;

			static constexpr int MAX_FRAME_SLOTS = 2;
			VkDescriptorPool perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];

			std::uint32_t frameCounter = 0;

			void InitRenderPass();
			void InitDescriptorSetLayout();
			void InitPipeline();
			void InitDescriptorPools();

			VkShaderModule LoadSPIRV(const char* path);

			VkFramebuffer MakeFramebuffer(VulkanImage* image, int frameSlot);

			VkDescriptorSet BindTextures(int frameSlot,
			                             VkImageView colorView,
			                             VkImageView depthView,
			                             VkSampler   depthSampler,
			                             VkImageView shadowView,
			                             VkSampler   shadowSampler,
			                             VkImageView coarseShadowView,
			                             VkSampler   coarseShadowSampler);

			// Fog2 variant — also binds the per-block AO and radiosity 3D
			// textures so the post-pass can integrate atmospheric scattering
			// from indirect light, matching GLFogFilter2.
			VkDescriptorSet BindTexturesFog2(int frameSlot,
			                                  VkImageView colorView,
			                                  VkImageView depthView,
			                                  VkSampler   depthSampler,
			                                  VkImageView shadowView,
			                                  VkSampler   shadowSampler,
			                                  VkImageView aoView,
			                                  VkSampler   aoSampler,
			                                  VkImageView radFlat,
			                                  VkImageView radX,
			                                  VkImageView radY,
			                                  VkImageView radZ,
			                                  VkSampler   radSampler);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanFogFilter(VulkanRenderer& renderer);
			~VulkanFogFilter();

			void Filter(VkCommandBuffer cmd,
			            VulkanImage* input, VulkanImage* output) override;
		};

	} // namespace draw
} // namespace spades
