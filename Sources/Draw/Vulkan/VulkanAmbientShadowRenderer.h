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

#include <array>
#include <atomic>
#include <vector>
#include <vulkan/vulkan.h>
#include <Draw/Vulkan/vk_mem_alloc.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace client {
		class GameMap;
	}
	namespace gui {
		class SDLVulkanDevice;
	}
	namespace draw {
		class VulkanRenderer;

		// Vulkan port of GLAmbientShadowRenderer.
		//
		// Maintains a per-block ambient occlusion 3D texture (RG float).
		// Computation is incremental: a background thread re-evaluates a few
		// dirty chunks per frame; ready chunks are uploaded the next time
		// Update() runs on the render command buffer. The image stays in
		// SHADER_READ_ONLY_OPTIMAL between frames; transitions only happen
		// when a chunk is uploaded.
		class VulkanAmbientShadowRenderer {
			class UpdateDispatch;

			static constexpr int NumRays = 16;
			static constexpr int ChunkSizeBits = 4;
			static constexpr int ChunkSize = 1 << ChunkSizeBits;
			static constexpr int RayLength = 16;

			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;
			Handle<client::GameMap> map;
			std::array<Vector3, NumRays> rays;

			struct Chunk {
				int cx, cy, cz;
				float data[ChunkSize][ChunkSize][ChunkSize][2];
				bool dirty = true;
				int dirtyMinX = 0, dirtyMaxX = ChunkSize - 1;
				int dirtyMinY = 0, dirtyMaxY = ChunkSize - 1;
				int dirtyMinZ = 0, dirtyMaxZ = ChunkSize - 1;
				std::atomic<bool> transferDone{true};
			};

			int w, h, d;
			int chunkW, chunkH, chunkD;

			std::vector<Chunk> chunks;

			VkImage image;
			VmaAllocation imageAllocation;
			VkImageView imageView;
			VkSampler sampler;

			// Per-chunk staging buffer ring; large enough to hold any single chunk
			// upload and reused across frames. (chunk = 16x16x16 RG float = 32 KB.)
			static constexpr int kStagingSlotCount = 16;
			VkBuffer stagingBuffer;
			VmaAllocation stagingAllocation;
			void* stagingMapped;
			std::size_t stagingSlotSize;
			int nextStagingSlot;

			UpdateDispatch* dispatch;

			Chunk& GetChunk(int cx, int cy, int cz) {
				return chunks[(cx + cy * chunkW) * chunkD + cz];
			}

			Chunk& GetChunkWrapped(int cx, int cy, int cz) {
				return GetChunk(cx & (chunkW - 1), cy & (chunkH - 1), cz);
			}

			void Invalidate(int minX, int minY, int minZ, int maxX, int maxY, int maxZ);

			void UpdateChunk(int cx, int cy, int cz);
			void UpdateDirtyChunks();
			int GetNumDirtyChunks();

			float Evaluate(IntVector3);

		public:
			VulkanAmbientShadowRenderer(VulkanRenderer& renderer, client::GameMap& map);
			~VulkanAmbientShadowRenderer();

			void GameMapChanged(int x, int y, int z, client::GameMap*);

			// Schedule a background recompute (if needed) and upload any chunks
			// the previous dispatch finished. Must be called outside a render pass.
			void Update(VkCommandBuffer commandBuffer);

			VkImageView GetImageView() const { return imageView; }
			VkSampler GetSampler() const { return sampler; }

			// Texture dimensions (image is w * h * (d+1)). Shaders divide world
			// coords by these to get the 3D texture coordinate.
			int GetWidth() const { return w; }
			int GetHeight() const { return h; }
			int GetDepthPlusOne() const { return d + 1; }
		};
	} // namespace draw
} // namespace spades
