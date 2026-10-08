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

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		struct SceneDefinition;
	}

	namespace draw {
		class VulkanBuffer;
		class VulkanDynamicLight;
		class VulkanImage;
		class VulkanRenderer;

		/**
		 * The frame's dynamic lights, laid out for the scene's lit shaders to take
		 * them in the pass that draws the surfaces, rather than in passes of their
		 * own: a table of the lights, and the view cut into clusters, each marking
		 * the lights that reach into it, which a compute pass works out every frame.
		 * A fragment reads only its cluster's lights (`DynamicLight/Lights.glsl`).
		 *
		 * Every frame in flight has its own table, clusters and descriptor set, so a
		 * frame's are written only once the GPU is done with them.
		 */
		class VulkanDynamicLightClusters {
		public:
			/** The most lights a frame takes (`DYNAMIC_LIGHT_MAX`); the rest are
			 * left out. */
			static constexpr std::uint32_t MaxLights = 256;
			/** The most spotlight images a frame takes (`DYNAMIC_LIGHT_IMAGES`); a
			 * spotlight with another one lights without it. */
			static constexpr std::uint32_t MaxImages = 4;

			/** The clusters across, up and deep the view is cut into. */
			static constexpr std::uint32_t ClustersX = 16;
			static constexpr std::uint32_t ClustersY = 9;
			static constexpr std::uint32_t ClustersZ = 24;

			/** Where the first slice of clusters starts ahead of the eye, in blocks:
			 * slices grow with depth, and nearer ones would cover next to nothing.
			 * Points nearer than that read every light. */
			static constexpr float ClusterNear = 0.5F;

			/** `local_size_x` of `DynamicLight/Cluster.comp` */
			static constexpr std::uint32_t ClusterGroupSize = 64;

			VulkanDynamicLightClusters(VulkanRenderer&, std::size_t framesInFlight);
			~VulkanDynamicLightClusters();

			VulkanDynamicLightClusters(const VulkanDynamicLightClusters&) = delete;
			VulkanDynamicLightClusters& operator=(const VulkanDynamicLightClusters&) = delete;

			/** The layout of the set the lit pipelines bind the lights to, as set
			 * `DYNAMIC_LIGHT_SET` (2). */
			VkDescriptorSetLayout GetSetLayout() const { return setLayout; }

			/** A layout without bindings, to stand for a set a pipeline layout has to
			 * list before the lights' but has nothing for. */
			VkDescriptorSetLayout GetEmptySetLayout() const { return emptySetLayout; }

			/** The set of frame slot `frameSlot`, holding what `Update` wrote. */
			VkDescriptorSet GetDescriptorSet(std::size_t frameSlot) const;

			/**
			 * Writes the frame's lights as `view` sees them into frame slot
			 * `frameSlot`'s table and records the pass binning them into its
			 * clusters, which the fragment shaders of `commandBuffer`'s render passes
			 * that follow can read. Records outside of a render pass.
			 */
			void Update(VkCommandBuffer commandBuffer, std::size_t frameSlot,
			            const std::vector<VulkanDynamicLight>& lights,
			            const client::SceneDefinition& view);

		private:
			struct Slot {
				Handle<VulkanBuffer> frame;
				Handle<VulkanBuffer> lights;
				Handle<VulkanBuffer> clusters;
				VkDescriptorSet set = VK_NULL_HANDLE;

				/** The images bound as `dynamicLightImages`, kept alive while the
				 * frame that reads them may be in flight */
				std::array<Handle<VulkanImage>, MaxImages> images;
			};

			VulkanRenderer& renderer;
			VkDevice device;

			VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
			VkDescriptorSetLayout emptySetLayout = VK_NULL_HANDLE;
			VkDescriptorPool pool = VK_NULL_HANDLE;
			VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
			VkPipeline pipeline = VK_NULL_HANDLE;

			std::vector<Slot> slots;

			void CreateSlot(Slot&);
			void CreatePipeline();

			/** Binds `images` as the slot's spotlight images, the white image in
			 * place of any missing, updating only those that changed. */
			void BindImages(Slot&, const std::array<VulkanImage*, MaxImages>& images);
		};
	} // namespace draw
} // namespace spades
