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

		// Push-constant blocks for the map pipelines, shared between the pipeline
		// layout (range size) and VulkanMapChunk (the actual push) so the two are
		// always sized from the same sizeof() and can never drift. std430 aligns
		// each vec3 to 16 bytes, hence the explicit trailing pad floats. An
		// undersized range silently drops the tail on AMD/amdvlk (fine on MoltenVK).
		struct MapSolidPushConstants { // physical lighting (192 bytes)
			Matrix4 projectionViewMatrix;
			Vector3 modelOrigin;   float fogDistance;
			Vector3 viewOrigin;    float _pad;
			Vector3 fogColor;      float _pad2;
			Vector3 sunDirection;  float _pad3; // _pad3 aligns viewMatrix to 16
			Matrix4 viewMatrix;
		};
		struct MapSolidPushConstantsBasic { // non-physical lighting (124 bytes)
			Matrix4 projectionViewMatrix;
			Vector3 modelOrigin;   float fogDistance;
			Vector3 viewOrigin;    float _pad;
			Vector3 fogColor;      float _pad2;
			Vector3 sunDirection;
		};
		struct MapDlightPushConstants { // dynamic light pass (224 bytes)
			Matrix4 projectionViewMatrix;
			Vector3 modelOrigin;           float fogDistance;
			Vector3 viewOrigin;            float lightRadius;
			Vector3 fogColor;              float lightRadiusInversed;
			Vector3 lightOrigin;           float lightTypeVal;
			Vector3 lightColor;            float lightLinearLength;
			Vector3 lightLinearDirection;  float _pad;
			Matrix4 lightSpotMatrix;
		};

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
			VkPipeline dlightPipeline;
			VkPipeline backfacePipeline;

			VkPipelineLayout pipelineLayout;
			VkPipelineLayout dlightPipelineLayout;
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
			void DrawColumnDynamicLight(VkCommandBuffer commandBuffer, int cx, int cy, int cz, Vector3 eye,
			                            const client::DynamicLightParam& light);

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
			void RenderDynamicLightPass(VkCommandBuffer commandBuffer, std::vector<void*> lights);
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
