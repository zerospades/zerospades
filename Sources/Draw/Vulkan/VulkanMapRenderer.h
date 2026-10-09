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

#include <vulkan/vulkan.h>
#include <vector>
#include <Client/IGameMapListener.h>
#include <Client/IRenderer.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanRenderer;
		class VulkanMapChunk;
		class VulkanBuffer;
		class VulkanImage;

		// The map pipelines' push constants: what changes from draw to draw, the
		// chunk's origin, as `DrawConstants` of BasicMap.vert and BasicMapPhys.vert
		// has it. The pass's view is `VulkanSceneView`'s. The pipeline layout's range
		// and the push are both sized from this, so they can never drift apart.
		struct MapDrawConstants {
			Vector3 modelOrigin;
			float _pad; // to the block's std430 size
		};
		static_assert(sizeof(MapDrawConstants) <= 128, "push constants past 128 bytes are not portable");
		class VulkanMapRenderer {

			friend class VulkanMapChunk;

		protected:
			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;

			bool physicalLighting;

			// Shader programs/pipelines
			VkPipeline depthonlyPipeline;
			VkPipeline basicPipeline;
			// Reversed-winding sibling of basicPipeline for the water reflection
			// (mirror) pass, whose negative-Z scale flips triangle winding.
			VkPipeline basicMirrorPipeline;
			VkPipeline backfacePipeline;

			VkPipelineLayout pipelineLayout;
			VkDescriptorSetLayout descriptorSetLayout;
			VkDescriptorPool descriptorPool;
			VkDescriptorSet textureDescriptorSet;

			Handle<VulkanImage> aoImage;

			Handle<VulkanBuffer> squareVertexBuffer;

			struct ChunkRenderInfo {
				bool rendered;
				float distance;
			};
			VulkanMapChunk** chunks;
			ChunkRenderInfo* chunkInfos;

			client::GameMap* gameMap;

			int numChunkWidth, numChunkHeight;
			int numChunkDepth, numChunks;

			inline int GetChunkIndex(int x, int y, int z) {
				return (x * numChunkHeight + y) * numChunkDepth + z;
			}

			inline VulkanMapChunk* GetChunk(int x, int y, int z) {
				return chunks[GetChunkIndex(x, y, z)];
			}

			void RealizeChunks(Vector3 eye);

			void DrawColumnDepth(VkCommandBuffer commandBuffer, int cx, int cy, int cz, Vector3 eye);
			void DrawColumnSunlight(VkCommandBuffer commandBuffer, int cx, int cy, int cz, Vector3 eye);

			void RenderBackface(VkCommandBuffer commandBuffer);

		public:
			VulkanMapRenderer(client::GameMap*, VulkanRenderer&);
			virtual ~VulkanMapRenderer();

			static void PreloadShaders(VulkanRenderer&);

			void GameMapChanged(int x, int y, int z, client::GameMap*);

			// Access to renderer for chunks
			VulkanRenderer& GetRenderer() { return renderer; }

			client::GameMap* GetMap() { return gameMap; }

			void Realize();
			void Prerender();
			void RenderSunlightPass(VkCommandBuffer commandBuffer);
			void RenderDepthPass(VkCommandBuffer commandBuffer);
			void RenderShadowMapPass(VkCommandBuffer commandBuffer, VkPipelineLayout shadowPipelineLayout);

			void CreatePipelines(VkRenderPass renderPass);
			void DestroyPipelines();

			// Writes the per-frame texture descriptor set:
			//   binding 0 = map (heightmap) shadow 2D texture
			//   binding 1 = per-block ambient occlusion 3D texture (radiosity path AO)
			//   binding 2 = radiosity flat (directional GI base) 3D texture
			//   binding 3 = radiosity X (per-axis directional GI) 3D texture
			//   binding 4 = radiosity Y (per-axis directional GI) 3D texture
			//   binding 5 = radiosity Z (per-axis directional GI) 3D texture
			//   binding 6 = 2D AmbientOcclusion atlas (no-radiosity path AO, GL parity)
			//               — sourced from Gfx/AmbientOcclusion.png via the cached
			//               aoImage member; not a parameter.
			void UpdateShadowDescriptor(VulkanImage* shadowImage,
			                            VkImageView aoView, VkSampler aoSampler,
			                            VkImageView radFlatView, VkImageView radXView,
			                            VkImageView radYView, VkImageView radZView,
			                            VkSampler radSampler);

			VkDescriptorSet GetShadowDescriptorSet() const { return textureDescriptorSet; }
			VkDescriptorSetLayout GetShadowDescriptorSetLayout() const { return descriptorSetLayout; }
		};
	} // namespace draw
} // namespace spades
