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
#include <Core/RefCountedObject.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanRenderer;
		class VulkanBuffer;
		class VulkanImage;
		class VulkanProgram;
		class VulkanPipeline;

		class VulkanWaterRenderer {
		private:
			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;
			client::GameMap* gameMap;

			// Shader program and pipeline
			VulkanProgram* waterProgram;
			Handle<VulkanPipeline> waterPipeline;
			VkDescriptorPool descriptorPool;
			std::vector<VkDescriptorSet> descriptorSets; // Per frame

			// Uniform buffers
			std::vector<Handle<VulkanBuffer>> waterMatricesUBOs; // Per frame

			// Push constants data (replaces WaterUBO for better performance)
			struct WaterPushConstants {
				Vector4 fogColor;
				Vector4 skyColor;
				Vector2 zNearFar;
				Vector2 _pad0;
				Vector4 fovTan;
				Vector4 waterPlane;
				Vector4 viewOriginVector;
				Vector2 displaceScale;
				Vector2 _pad1;
				Vector4 sunDirection;
			} waterPushConstants;
			static_assert(sizeof(WaterPushConstants) == 128,
			              "WaterPushConstants must stay 128 bytes; the shader push_constant "
			              "block and the reflected pipeline range depend on this std430 layout");

		public:
			VulkanWaterRenderer(VulkanRenderer& r, client::GameMap* map);
			~VulkanWaterRenderer();

			static void PreloadShaders(VulkanRenderer&);

			void GameMapChanged(int x, int y, int z, client::GameMap*);
			void SetGameMap(client::GameMap* map);

			void Realize();
			void Prerender();

			void RenderSunlightPass(VkCommandBuffer commandBuffer);
			void RenderDepthPass(VkCommandBuffer commandBuffer);

			// Water update tick
			void UpdateSimulation(float dt);
			void UploadWaveTextures(VkCommandBuffer commandBuffer);

			// Mark parts of the water color texture to be updated
			void MarkUpdate(int x, int y);

		private:
			void CreateDescriptorPool();
			void CreateDescriptorSets();
			// (Re)writes the per-frame static descriptors (mainTexture, waveTexture,
			// matrices UBO). Safe to call again after the color/wave images are
			// (re)created post-construction — e.g. when the map loads after the
			// renderer, which otherwise leaves mainTexture unbound (black water).
			void RebindStaticTextures();
			void CreateUniformBuffers();
			void UpdateUniformBuffers(uint32_t frameIndex);
			void CleanupDescriptorResources();

			// Mesh
			Handle<VulkanBuffer> vertexBuffer;
			Handle<VulkanBuffer> indexBuffer;
			unsigned int numIndices;

			// Water color texture (map colors)
			Handle<VulkanImage> textureImage;
			int w, h;
			int updateBitmapPitch;
			std::vector<uint32_t> updateBitmap;
			std::vector<uint32_t> bitmap;

			// Wave height textures
			Handle<VulkanImage> waveImage;       // Single layer (r_water < 2)
			Handle<VulkanImage> waveImageArray;  // Multiple layers (r_water >= 2)
			std::vector<void*> waveTanksPlaceholder;

			// Staging buffer pool for wave uploads
			std::vector<Handle<VulkanBuffer>> waveStagingBufferPool;
			size_t waveStagingBufferSize;

			// Occlusion query for skipping water rendering when not visible
			VkQueryPool occlusionQueryPool;
			bool occlusionQueryActive;
			uint64_t lastOcclusionResult;
		};
	} // namespace draw
} // namespace spades
