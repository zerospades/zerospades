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

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>
#include "VulkanPostProcessFilter.h"

namespace spades {
	namespace draw {

		// Resolves a multisampled depth attachment into a single-sample
		// R32_SFLOAT colour image (sample 0). Only used when MSAA is on, because
		// vkCmdResolveImage does not support depth. See DepthResolve.vk.fs for why
		// the result is a colour image rather than a depth target.
		//
		// Call Resolve(cmd, msaaDepth, output):
		//   * msaaDepth must be in SHADER_READ_ONLY_OPTIMAL (depth aspect view).
		//   * output is an R32_SFLOAT colour image; it ends in
		//     SHADER_READ_ONLY_OPTIMAL, ready for the depth-reading filters / water.
		//
		// The output format (R32_SFLOAT) is the canonical resolved-depth format
		// used across the renderer; exposed so callers can create matching images.
		class VulkanDepthResolveFilter : public VulkanPostProcessFilter {

			VkSampler depthSampler; // nearest, clamp-to-edge (texelFetch ignores it)

			VkRenderPass ppRenderPass;
			VkDescriptorSetLayout msDepthDSL;

			VkPipelineLayout resolveLayout;
			VkPipeline resolvePipeline;

			static constexpr int MAX_FRAME_SLOTS = 2;
			VkDescriptorPool perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];
			// Monotonic frame number a slot was last reset on, so we reset a slot's
			// pool/framebuffers only on its first Resolve() each frame and can safely
			// resolve more than once per frame (e.g. mirror depth + scene depth).
			std::uint32_t slotResetFrame[MAX_FRAME_SLOTS];

			void InitRenderPass();
			void InitDescriptorSetLayout();
			void InitPipeline();
			void InitDescriptorPools();

			VkShaderModule LoadSPIRV(const char* path);
			VkFramebuffer MakeFramebuffer(VulkanImage* image, int frameSlot);
			VkDescriptorSet BindDepth(int frameSlot, VkImageView view);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

			// Base Filter() is not meaningful here (input is multisampled depth);
			// use Resolve() instead. Kept as a no-op to satisfy the interface.
			void Filter(VkCommandBuffer, VulkanImage*, VulkanImage*) override {}

		public:
			static constexpr VkFormat kResolvedDepthFormat = VK_FORMAT_R32_SFLOAT;

			VulkanDepthResolveFilter(VulkanRenderer& renderer);
			~VulkanDepthResolveFilter();

			void Resolve(VkCommandBuffer cmd, VulkanImage* msaaDepth, VulkanImage* output);
		};

	} // namespace draw
} // namespace spades
