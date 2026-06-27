/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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

		// Sun lens flare filter (Vulkan port of GLLensFlareFilter).
		//
		// Sub-passes (all in one Filter() call):
		//   1. Visibility scanner — renders a soft sun disc into a 64×64
		//      RGBA temp image by shadow-comparing the offscreen depth
		//      texture against the sun's NDC Z.
		//   2. Three 1D-Gaussian blurs (spread = 1, 2, 4), x then y.
		//   3. Passthrough + additive flare composition into `output` —
		//      copies the scene input across, then draws ~13 additive
		//      sprites (flare + dust + reflections) on top, mirroring
		//      the GL filter's quad list and tint values.
		//
		// Call Filter(cmd, input, output).  input must be in
		// SHADER_READ_ONLY_OPTIMAL; output ends in SHADER_READ_ONLY_OPTIMAL.

		class VulkanLensFlareFilter : public VulkanPostProcessFilter {

			VkFormat colorFormat;

			VkSampler linearSampler;

			VkRenderPass scannerRenderPass;
			VkRenderPass blurRenderPass;
			VkRenderPass finalRenderPass;

			VkDescriptorSetLayout shadowSamplerDSL;
			VkDescriptorSetLayout singleSamplerDSL;
			VkDescriptorSetLayout tripleSamplerDSL;

			VkPipelineLayout scannerLayout;
			VkPipelineLayout blurLayout;
			VkPipelineLayout passthroughLayout;
			VkPipelineLayout flareDrawLayout;

			VkPipeline scannerPipeline;
			VkPipeline blurPipeline;
			VkPipeline passthroughPipeline;
			VkPipeline flareDrawPipeline;

			static constexpr int MAX_FRAME_SLOTS = 2;
			VkDescriptorPool perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];

			// Lazy-loaded sprite textures (registered in ctor).
			Handle<client::IImage> flare1, flare2, flare3, flare4;
			Handle<client::IImage> mask1, mask2, mask3;
			Handle<client::IImage> white;

			void InitRenderPasses();
			void InitDescriptorSetLayouts();
			void InitPipelines();
			void InitDescriptorPools();
			void InitSamplers();
			void LoadSpriteTextures();

			VkShaderModule LoadSPIRV(const char* path);

			VkFramebuffer MakeFramebuffer(VkRenderPass rp, VulkanImage* image, int frameSlot);

			VkDescriptorSet BindShadowDepth(int frameSlot, VkImageView depthView,
			                                VkSampler depthSampler);
			VkDescriptorSet BindSingleTexture(int frameSlot, VkImageView view);
			VkDescriptorSet BindFlareTextures(int frameSlot,
			                                   VkImageView visibility,
			                                   VkImageView modulation,
			                                   VkImageView flare);

			// Emit a six-vertex flare quad with the given draw range, tint
			// and three texture bindings (visibility / modulation / flare).
			void DrawFlareQuad(VkCommandBuffer cmd, int frameSlot,
			                    VkImageView visibility,
			                    const Handle<client::IImage>& modulation,
			                    const Handle<client::IImage>& flare,
			                    const float drawRange[4],
			                    const float color[3]);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanLensFlareFilter(VulkanRenderer& renderer);
			~VulkanLensFlareFilter();

			void Filter(VkCommandBuffer cmd,
			            VulkanImage* input, VulkanImage* output) override;
		};

	} // namespace draw
} // namespace spades
