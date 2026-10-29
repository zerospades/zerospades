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

#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

#include "VulkanSceneView.h"
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		struct SceneDefinition;
	}
	namespace draw {
		class VulkanImage;
		class VulkanRenderer;

		/**
		 * Screen-space ambient occlusion (`r_ssao`), as GL's GLSSAOFilter works it
		 * out and its lit shaders apply it to the ambient light, for the main view
		 * and the water's mirror.
		 *
		 * Compute, from a view's depth pre-pass: one dispatch evaluates the
		 * occlusion (`AmbientOcclusion/Evaluate.comp`), at the render size or, for
		 * the mirror and at `r_ssao` 2, at half of it; depth-aware blurs along
		 * one axis at a time (`Blur.comp`) bring it to the render size, into the
		 * view's output for the frame slot, which the lit pass then reads texel
		 * for texel (`SceneAmbientOcclusion.glsl`).
		 *
		 * Everything is sized to the render targets, and remade with them. Every
		 * descriptor is written once, at creation. The intermediates are shared by
		 * the views and the frames in flight, kept in GENERAL, and ordered by
		 * barriers (on the one queue, a barrier orders against every earlier
		 * command, the previous frame's included); the outputs are a frame slot's
		 * own, in SHADER_READ_ONLY_OPTIMAL between frames.
		 */
		class VulkanScreenSpaceAmbientOcclusion {
		public:
			/** The format of the occlusion, one the core guarantees for storage */
			static constexpr VkFormat Format = VK_FORMAT_R32_SFLOAT;

			VulkanScreenSpaceAmbientOcclusion(VulkanRenderer&, std::size_t framesInFlight);
			~VulkanScreenSpaceAmbientOcclusion();

			VulkanScreenSpaceAmbientOcclusion(const VulkanScreenSpaceAmbientOcclusion&) = delete;
			VulkanScreenSpaceAmbientOcclusion&
			operator=(const VulkanScreenSpaceAmbientOcclusion&) = delete;

			/** Whether it has `view`'s: the mirror's only with a mirror to draw */
			bool HasView(VulkanSceneView::View view) const;

			/** The occlusion `Render` leaves for `view` of frame slot `frameSlot`:
			 * the render size, in SHADER_READ_ONLY_OPTIMAL */
			VulkanImage& GetOutput(std::size_t frameSlot, VulkanSceneView::View view);

			/**
			 * Records `view`'s occlusion into its output of frame slot `frameSlot`,
			 * outside of a render pass, from the depth of its pre-pass, sampled as
			 * the post filters do: its R32F resolve under MSAA, its R32F copy at 1x
			 * (`GetResolvedDepthImage`, `GetMirrorResolvedDepthImage`), in
			 * SHADER_READ_ONLY_OPTIMAL and visible to compute.
			 */
			void Render(VkCommandBuffer, std::size_t frameSlot, VulkanSceneView::View view,
			            const client::SceneDefinition& sceneDef);

		private:
			struct Pass {
				VkDescriptorSet set = VK_NULL_HANDLE;
				VulkanImage* target = nullptr;
				// Blurs only: along X or Y, from a source of 1 or 2 depth texels
				int axis = 0;
				int sourceScale = 1;
			};
			struct ViewResources {
				bool present = false;
				// What it reads: the framebuffer manager's, which lives as long
				VulkanImage* depth = nullptr;
				// Depth texels per evaluated texel: 1 or 2
				int scale = 1;
				Handle<VulkanImage> evaluated;
				std::vector<Handle<VulkanImage>> outputs; // a frame slot's each
				// A frame slot's each: the evaluation, then the blurs
				std::vector<std::vector<Pass>> passes;
			};

			VulkanRenderer& renderer;
			VkDevice device;
			VkSampler depthSampler = VK_NULL_HANDLE;
			VkDescriptorSetLayout evaluateSetLayout = VK_NULL_HANDLE;
			VkDescriptorSetLayout blurSetLayout = VK_NULL_HANDLE;
			VkPipelineLayout evaluateLayout = VK_NULL_HANDLE;
			VkPipelineLayout blurLayout = VK_NULL_HANDLE;
			VkPipeline evaluatePipeline = VK_NULL_HANDLE;
			VkPipeline blurPipeline = VK_NULL_HANDLE;
			VkDescriptorPool pool = VK_NULL_HANDLE;

			// The blurs' intermediates, at the render size
			Handle<VulkanImage> ping, pong;
			ViewResources views[VulkanSceneView::ViewCount];

			Handle<VulkanImage> MakeImage(std::uint32_t width, std::uint32_t height, bool sampled);
			void CreatePipelines();
			void CreateViews(std::size_t framesInFlight);
			void InitializeLayouts();
			VkDescriptorSet MakeEvaluateSet(const ViewResources&);
			VkDescriptorSet MakeBlurSet(const ViewResources&, VulkanImage& source,
			                            VulkanImage& target);
		};
	} // namespace draw
} // namespace spades
