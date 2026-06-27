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

#include <atomic>
#include <cstdint>
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

		// Vulkan port of GLRadiosityRenderer. Four packed 10:10:10:2 3D
		// textures (flat / X / Y / Z) carrying per-block directional
		// indirect-light contributions; refined incrementally on a worker
		// thread. Format A2R10G10B10 mirrors GL's BGRA + 2_10_10_10_REV
		// reorder so the GL shader port reads identical channel values.
		class VulkanRadiosityRenderer {
			class UpdateDispatch;

			using VoxelType = std::uint32_t;
			static constexpr int ChunkSizeBits = 4;
			static constexpr int ChunkSize = 1 << ChunkSizeBits;
			static constexpr int Envelope = 6;

			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;
			Handle<client::GameMap> map;

			struct Chunk {
				int cx, cy, cz;
				VoxelType dataFlat[ChunkSize][ChunkSize][ChunkSize];
				VoxelType dataX[ChunkSize][ChunkSize][ChunkSize];
				VoxelType dataY[ChunkSize][ChunkSize][ChunkSize];
				VoxelType dataZ[ChunkSize][ChunkSize][ChunkSize];
				bool dirty = true;
				int dirtyMinX = 0, dirtyMaxX = ChunkSize - 1;
				int dirtyMinY = 0, dirtyMaxY = ChunkSize - 1;
				int dirtyMinZ = 0, dirtyMaxZ = ChunkSize - 1;
				std::atomic<bool> transferDone{true};
			};

			int w, h, d;
			int chunkW, chunkH, chunkD;
			std::vector<Chunk> chunks;

			VkImage imageFlat, imageX, imageY, imageZ;
			VmaAllocation allocFlat, allocX, allocY, allocZ;
			VkImageView viewFlat, viewX, viewY, viewZ;
			VkSampler sampler;

			// Staging ring large enough to fit dataFlat+X+Y+Z for one chunk
			// per slot. 128 slots × (4 × 16³ × 4 B) = 8 MiB. Sized to match
			// the bumped UpdateDirtyChunks per-dispatch budget so the GPU
			// upload doesn't bottleneck the convergence rate.
			static constexpr int kStagingSlotCount = 128;
			VkBuffer stagingBuffer;
			VmaAllocation stagingAllocation;
			void* stagingMapped;
			std::size_t stagingPerTextureSize; // bytes per dst texture in one slot
			std::size_t stagingSlotSize;       // 4 × stagingPerTextureSize
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

			std::uint32_t EncodeValue(Vector3 vec);
			float CompressDynamicRange(float v);

		public:
			struct Result {
				Vector3 base, x, y, z;
			};

			VulkanRadiosityRenderer(VulkanRenderer& renderer, client::GameMap& map);
			~VulkanRadiosityRenderer();

			Result Evaluate(IntVector3);
			void GameMapChanged(int x, int y, int z, client::GameMap*);

			// Schedule a background recompute (if needed) and upload any
			// chunks the previous dispatch finished. Must be called outside
			// a render pass.
			void Update(VkCommandBuffer commandBuffer);

			VkImageView GetImageViewFlat() const { return viewFlat; }
			VkImageView GetImageViewX() const { return viewX; }
			VkImageView GetImageViewY() const { return viewY; }
			VkImageView GetImageViewZ() const { return viewZ; }
			VkSampler GetSampler() const { return sampler; }
		};
	} // namespace draw
} // namespace spades
