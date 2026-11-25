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
		//
		// ResolveStencil(cmd, msaaDepth, output) does the same for the stencil
		// aspect, through its sampled view (VulkanImage::GetStencilImageView), into
		// an R8_UINT image (VulkanFramebufferManager::kSampledStencilFormat).
		class VulkanDepthResolveFilter : public VulkanPostProcessFilter {

			VkSampler depthSampler; // nearest, clamp-to-edge (texelFetch ignores it)

			VkRenderPass ppRenderPass;
			VkRenderPass stencilRenderPass; // into an R8_UINT target
			VkDescriptorSetLayout msDepthDSL;

			VkPipelineLayout resolveLayout;
			VkPipeline resolvePipeline;
			VkPipeline stencilResolvePipeline;

			// A depth or stencil resolved into an output: the scene's depth and
			// stencil, the mirror's depth. Its
			// framebuffer and descriptor set are made on its first resolve and kept
			// until the targets go (`ReleaseTargets`); they never change, so frames in
			// flight share them.
			struct Target {
				VulkanImage* output;
				// The multisampled aspect's sampled view
				VkImageView sourceView;
				bool stencil;
				VkFramebuffer framebuffer;
				VkDescriptorSet set;
				// Whether it holds the depth as it is now: set by a resolve, cleared
				// by `Invalidate`
				bool resolved;
			};
			static constexpr std::uint32_t kMaxTargets = 4;
			std::vector<Target> targets;
			VkDescriptorPool descPool;

			void InitRenderPass();
			void InitDescriptorSetLayout();
			void InitPipeline();
			void InitDescriptorPool();

			VkShaderModule LoadSPIRV(const char* path);
			VkPipeline CreateResolvePipeline(const char* fragmentShader, VkRenderPass renderPass);
			Target& GetTarget(VkImageView sourceView, bool stencil, VulkanImage* output);
			void Draw(VkCommandBuffer cmd, Target& target);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

			// Base Filter() is not meaningful here (input is multisampled depth);
			// use Resolve() instead. Kept as a no-op to satisfy the interface.
			void Filter(VkCommandBuffer, VulkanImage*, VulkanImage*) override {}

		public:
			static constexpr VkFormat kResolvedDepthFormat = VK_FORMAT_R32_SFLOAT;

			VulkanDepthResolveFilter(VulkanRenderer& renderer);
			~VulkanDepthResolveFilter();

			/** Resolves `msaaDepth` into `output`, unless it already did since the
			 * depth last changed (`Invalidate`) */
			void Resolve(VkCommandBuffer cmd, VulkanImage* msaaDepth, VulkanImage* output);
			/** Resolves the stencil of `msaaDepth`, in SHADER_READ_ONLY_OPTIMAL, into
			 * `output`, an R8_UINT image left in SHADER_READ_ONLY_OPTIMAL. Every
			 * call resolves: call it once the stencil's last stamp is drawn. */
			void ResolveStencil(VkCommandBuffer cmd, VulkanImage* msaaDepth,
			                    VulkanImage* output);
			/** The depths may have changed since their last resolve: each is
			 * resolved again */
			void Invalidate();
			/** Lets go of the framebuffers and sets made for the targets, before
			 * those are replaced. Nothing may be in flight. */
			void ReleaseTargets();
		};

	} // namespace draw
} // namespace spades
