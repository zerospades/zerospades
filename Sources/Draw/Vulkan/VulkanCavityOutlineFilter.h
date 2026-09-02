/*
 Copyright (c) 2026 Fran6nd

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

		// Screen-space cavity / silhouette outline filter.
		//
		// One fullscreen post-process pass that detects edges by
		// sampling the offscreen depth buffer at the centre and four
		// cardinal neighbours, reconstructing world-space positions
		// from each tap, and measuring how far the neighbours fall
		// from the local surface plane at the centre. Where that
		// out-of-plane distance is large compared to the typical
		// pixel-to-pixel tangent length (i.e. there is a depth
		// discontinuity), a black overlay is composited over the
		// input colour image with an alpha that fades smoothly into
		// the haze using fogDistance.
		//
		// This replaces the per-renderer outline machinery (map +
		// model wireframe / inverted-hull). Why:
		//
		//   * wideLines + POLYGON_MODE_LINE: wideLines is unsupported
		//     on MoltenVK, and Metal's line-rasterisation shim
		//     flickers at silhouettes.
		//   * Inverted-hull extrusion: voxel meshes have per-face
		//     vertex copies pushed in different directions at shared
		//     corners — leaves visible seams on every cube corner.
		//   * Geometry-shader extrusion: geometryShader is
		//     unsupported on MoltenVK at all.
		//
		// Cost is O(pixels) — independent of voxel count — and the
		// implementation is just a fragment shader sampling a 2D
		// image, so it works on every Vulkan implementation including
		// MoltenVK.
		//
		// Descriptor bindings (set 0):
		//   0 — colour input  (combined sampler, SHADER_READ_ONLY)
		//   1 — depth         (combined sampler, SHADER_READ_ONLY,
		//                      depth aspect)
		//
		// Filter(cmd, input, output): input must be in
		// SHADER_READ_ONLY_OPTIMAL; output ends in
		// SHADER_READ_ONLY_OPTIMAL. The depth image must also be in
		// SHADER_READ_ONLY_OPTIMAL on entry — the dispatch site
		// already transitions it for the fog filter, so we get it
		// for free.
		class VulkanCavityOutlineFilter : public VulkanPostProcessFilter {

			VkFormat colorFormat;
			VkSampler colorSampler; // linear, clamp-to-edge

			VkRenderPass ppRenderPass;

			VkDescriptorSetLayout dualSamplerDSL; // bindings 0, 1

			VkPipelineLayout cavityLayout;
			VkPipeline       cavityPipeline;

			static constexpr int MAX_FRAME_SLOTS = 2;
			VkDescriptorPool perFrameDescPool[MAX_FRAME_SLOTS];
			std::vector<VkFramebuffer> perFrameFramebuffers[MAX_FRAME_SLOTS];

			void InitRenderPass();
			void InitDescriptorSetLayout();
			void InitPipeline();
			void InitDescriptorPools();

			VkShaderModule LoadSPIRV(const char* path);

			VkFramebuffer MakeFramebuffer(VulkanImage* image, int frameSlot);

			VkDescriptorSet BindTextures(int         frameSlot,
			                             VkImageView colorView,
			                             VkImageView depthView,
			                             VkSampler   depthSampler);

			void CreatePipeline() override {}
			void CreateRenderPass() override {}

		public:
			VulkanCavityOutlineFilter(VulkanRenderer& renderer);
			~VulkanCavityOutlineFilter();

			void Filter(VkCommandBuffer cmd,
			            VulkanImage* input, VulkanImage* output) override;
		};

	} // namespace draw
} // namespace spades
