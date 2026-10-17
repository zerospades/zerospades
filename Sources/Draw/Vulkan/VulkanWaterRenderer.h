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

#include "VulkanPushConstants.h"

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
			static_assert(sizeof(WaterPushConstants) <= kPortablePushConstantsSize,
			              "push constants must fit the 128 bytes every device has (VulkanPushConstants.h)");

		public:
			VulkanWaterRenderer(VulkanRenderer& r, client::GameMap* map);
			~VulkanWaterRenderer();

			static void PreloadShaders(VulkanRenderer&);

			void GameMapChanged(int x, int y, int z, client::GameMap*);
			void SetGameMap(client::GameMap* map);

			void Realize();

			void RenderSunlightPass(VkCommandBuffer commandBuffer);

			/** Starts this frame's occlusion query when `count`, the reflections
			 * asking for it (`WasRecentlySeen`): resets this frame slot's, outside
			 * any render pass, before the water is drawn in it (`RenderSunlightPass`),
			 * which then counts what of it passes the depth test. */
			void BeginOcclusionQueryFrame(VkCommandBuffer commandBuffer, bool count);
			/**
			 * Whether any of the water passed the depth test when last counted, as
			 * GL's `r_occlusionQuery` asks before drawing the reflections: the last
			 * frame's count if the GPU is done with it already, else the older one
			 * of this frame slot, whose frame has been waited on. Neither waits;
			 * unknown counts as seen.
			 */
			bool WasRecentlySeen();

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
			/** Makes what the water draws from the map with: its colour image, the
			 * wave tanks, images and staging */
			void CreateMapResources();
			void CreateUniformBuffers();
			void UpdateUniformBuffers(uint32_t frameIndex);
			void CleanupDescriptorResources();

			// One occlusion query per frame in flight around the water's draw,
			// whether a slot's query was drawn in its last use and in this one, and
			// whether this frame counts
			VkQueryPool occlusionQueryPool = VK_NULL_HANDLE;
			std::vector<bool> occlusionQueryIssuedLastUse;
			std::vector<bool> occlusionQueryIssued;
			bool countingThisFrame = false;

			/** The count of slot `slot`'s query into `seen`, if it is available;
			 * never waits */
			bool ReadOcclusionQuery(uint32_t slot, bool& seen);

			// Mesh
			Handle<VulkanBuffer> vertexBuffer;
			Handle<VulkanBuffer> indexBuffer;
			// Must be initialised here: the constructor returns early when the
			// renderer is built before a map is loaded, and Realize() only
			// builds the mesh while this is 0. A leftover non-zero value skips
			// the build, then RenderSunlightPass gets past its own == 0 guard
			// and dereferences the null vertexBuffer.
			unsigned int numIndices = 0;

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
			// Per frame in flight, then per layer
			std::vector<Handle<VulkanBuffer>> waveStagingBufferPool;
			size_t waveStagingBufferSize;
		};
	} // namespace draw
} // namespace spades
