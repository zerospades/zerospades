/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

#include "VulkanFramesInFlight.h"
#include "VulkanPostProcessFilter.h"
#include <Core/Math.h>
#include <Core/RefCountedObject.h>
#include <Draw/TemporalAAJitter.h>

namespace spades {
	namespace draw {

		/**
		 * Temporal anti-aliasing (`r_temporalAA`, GL's `GLTemporalAAFilter`).
		 *
		 * The main view is drawn with a sub-pixel jitter (`NextJitter`, GL's sequence).
		 * `Filter` reprojects each pixel into the last frame through the scene's
		 * depth and the last frame's unjittered view-projection, clamps the history
		 * found there to the pixel's neighbourhood in YCoCg-like space, and blends
		 * it in, as GL's TemporalAA.fs does. First-person pixels (depth below
		 * `kFirstPersonDepthEnd`) are taken as fixed to the screen.
		 *
		 * One fragment pass writes both the chain's `output` and the history the
		 * next frame reads: two RGBA16F images used in turn, the blend rate in their
		 * alpha (the scene's own 10:10:10:2 format has no room for it). Nothing is
		 * copied; render-pass dependencies order a frame's write of one after the
		 * last frame's read of it. The history starts over on a cut, a change of
		 * size (resize, r_scale) or a frame without temporal AA.
		 */
		class VulkanTemporalAAFilter : public VulkanPostProcessFilter {
			VkFormat colorFormat;
			static constexpr VkFormat kHistoryFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

			VkSampler linearSampler;
			VkSampler nearestSampler;
			VkRenderPass resolvePass;
			VkDescriptorSetLayout setLayout;
			VkPipelineLayout resolveLayout;
			VkPipeline resolvePipeline;

			static constexpr int MAX_FRAME_SLOTS = MaxFramesInFlight;
			VkDescriptorPool perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];

			Handle<VulkanImage> history[2];
			int historyWritten = 0; // the image the last frame wrote
			bool historyValid = false;
			std::uint32_t lastFrame = 0;

			// The last frame's view-centric view-projection onto texture coordinates,
			// and where its eye was (kept apart from the matrix, as GL does, for
			// precision)
			Matrix4 prevMatrix;
			Vector3 prevViewOrigin;

			TemporalAAJitter jitter;

			void InitRenderPass();
			void InitDescriptorSetLayout();
			void InitPipeline();
			void InitDescriptorPools();
			VkShaderModule LoadSPIRV(const char* path);
			void PrepareHistory(VkCommandBuffer cmd, uint32_t width, uint32_t height);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanTemporalAAFilter(VulkanRenderer& renderer);
			~VulkanTemporalAAFilter();

			/** The offset to draw the next frame's main view at
			 * (`TemporalAAJitter::Next`) */
			Vector2 NextJitter(int width, int height) { return jitter.Next(width, height); }

			void Filter(VkCommandBuffer cmd, VulkanImage* input, VulkanImage* output) override;
		};

	} // namespace draw
} // namespace spades
