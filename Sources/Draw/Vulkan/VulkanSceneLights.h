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
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

#include <Core/Math.h>
#include <Core/RefCountedObject.h>
#include <Draw/Vulkan/VulkanMemoryAllocator.h>

namespace spades {
	namespace client {
		struct SceneDefinition;
	}

	namespace draw {
		class VulkanBuffer;
		class VulkanDynamicLight;
		class VulkanImage;
		class VulkanMapOccupancy;
		class VulkanRenderer;

		/**
		 * The scene's lights, as the lit pipelines bind them (set 2): the sun's and
		 * the sky's of the frame, and its dynamic lights.
		 *
		 * The dynamic lights are laid out for the scene's lit shaders to take them in
		 * the pass that draws the surfaces, rather than in passes of their own: a
		 * table of the lights, and the view cut into clusters, each marking
		 * the lights that reach into it, which a compute pass works out every frame.
		 * A fragment reads only its cluster's lights (`SceneLight/Lights.glsl`).
		 *
		 * The map hides the lights, and each spotlight whose beam it can afford gets
		 * a tile of an occlusion map, traced by another compute pass every frame:
		 * every texel holds how far the ray through it gets from the light before it
		 * enters a solid block. Four reads of it prove most lit points lit, where they
		 * would otherwise walk the map themselves, light by light.
		 *
		 * Every frame in flight has its own table, clusters and descriptor set, so a
		 * frame's are written only once the GPU is done with them.
		 */
		class VulkanSceneLights {
		public:
			/** The sun's and the sky's light of the frame: `SceneSunSky` of
			 * `SceneLight/SunSky.glsl`. */
			struct SunSky {
				/** The factor the sun's light is drawn with, in `[0, 1]` */
				float sunlight = 1.0F;
				/** The factor the sky's light is drawn with, in `[0, 1]` */
				float daylight = 1.0F;
				/** The sky's light falling on surfaces in full daylight, linear */
				Vector3 skyLight = MakeVector3(0.0F, 0.0F, 0.0F);
				/** The ambient light the radiosity adds in full daylight, linear */
				Vector3 ambientLight = MakeVector3(0.0F, 0.0F, 0.0F);
			};

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

			/** `local_size_x` of `SceneLight/Cluster.comp` */
			static constexpr std::uint32_t ClusterGroupSize = 64;

			/** The texels along an occlusion map tile's edge
			 * (`DYNAMIC_LIGHT_OCCLUSION_TILE`) */
			static constexpr std::uint32_t OcclusionTileSize = 128;
			/** The tiles along the atlas's edge (`DYNAMIC_LIGHT_OCCLUSION_TILES_PER_ROW`) */
			static constexpr std::uint32_t OcclusionTilesPerRow = 8;
			/** `DYNAMIC_LIGHT_OCCLUSION_MAX_TILES`; any further spotlight walks the map. */
			static constexpr std::uint32_t MaxOcclusionTiles =
			  OcclusionTilesPerRow * OcclusionTilesPerRow;
			static constexpr std::uint32_t OcclusionAtlasSize =
			  OcclusionTileSize * OcclusionTilesPerRow;
			/** `local_size_x` and `local_size_y` of `SceneLight/OcclusionMap.comp` */
			static constexpr std::uint32_t OcclusionGroupSize = 8;

			/**
			 * How much wider a texel of a light's occlusion map gets per block away
			 * from it: the tile spans the light's image out to where its cone fades.
			 */
			static float GetOcclusionTexelSpread(const VulkanDynamicLight&);

			/** Whether a light can have an occlusion map: a spotlight whose texels stay
			 * small enough over its whole reach. */
			static bool IsOcclusionMappable(const VulkanDynamicLight&);

			VulkanSceneLights(VulkanRenderer&, std::size_t framesInFlight);
			~VulkanSceneLights();

			VulkanSceneLights(const VulkanSceneLights&) = delete;
			VulkanSceneLights& operator=(const VulkanSceneLights&) = delete;

			/** The layout of the set the lit pipelines bind the lights to, as set
			 * `SCENE_LIGHT_SET` (2). */
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
			 * that follow can read. The map hides the lights by `occupancy`, or
			 * nothing does without one. The sun and the sky light the scene as
			 * `sunSky` says. Records outside of a render pass.
			 */
			void Update(VkCommandBuffer commandBuffer, std::size_t frameSlot,
			            const std::vector<VulkanDynamicLight>& lights,
			            const client::SceneDefinition& view, const VulkanMapOccupancy* occupancy,
			            const SunSky& sunSky);

		private:
			struct Slot {
				Handle<VulkanBuffer> frame;
				Handle<VulkanBuffer> sunSky;
				Handle<VulkanBuffer> lights;
				Handle<VulkanBuffer> clusters;
				VkDescriptorSet set = VK_NULL_HANDLE;

				/** The images bound as `dynamicLightImages`, kept alive while the
				 * frame that reads them may be in flight */
				std::array<Handle<VulkanImage>, MaxImages> images;

				/** The occlusion maps' atlas, in `VK_IMAGE_LAYOUT_GENERAL` once used */
				VkImage occlusionAtlas = VK_NULL_HANDLE;
				VmaAllocation occlusionAtlasAllocation = VK_NULL_HANDLE;
				VkImageView occlusionAtlasView = VK_NULL_HANDLE;
				bool occlusionAtlasInitialized = false;
			};

			VulkanRenderer& renderer;
			VkDevice device;

			VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
			VkDescriptorSetLayout emptySetLayout = VK_NULL_HANDLE;
			VkDescriptorPool pool = VK_NULL_HANDLE;
			VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
			/** Bins the lights into the clusters */
			VkPipeline pipeline = VK_NULL_HANDLE;
			/** Traces the occlusion maps */
			VkPipeline occlusionPipeline = VK_NULL_HANDLE;

			std::vector<Slot> slots;

			/** Bound in place of the map's occupancy without a map */
			std::unique_ptr<VulkanMapOccupancy> noOccupancy;

			void CreateSlot(Slot&);
			void CreatePipelines();
			VkPipeline CreateComputePipeline(const char* shaderPath);
			void CreateOcclusionAtlas(Slot&);

			/** Binds `images` as the slot's spotlight images, the white image in
			 * place of any missing, updating only those that changed. */
			void BindImages(Slot&, const std::array<VulkanImage*, MaxImages>& images);

			/** Binds `occupancy` as the slot's map to walk. */
			void BindOccupancy(Slot&, const VulkanMapOccupancy& occupancy);
		};
	} // namespace draw
} // namespace spades
