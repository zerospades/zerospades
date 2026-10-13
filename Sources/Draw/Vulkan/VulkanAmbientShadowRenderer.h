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
#include <cstddef>
#include <mutex>
#include <vector>
#include <vulkan/vulkan.h>
#include <Draw/Vulkan/VulkanMemoryAllocator.h>
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
			/** Whether it lights the scene (`r_radiosity`). Otherwise its volume
			 * are 1x1x1 placeholders, for the descriptors the lit pipelines declare
			 * but the shaders, specialized without radiosity, never read: no
			 * whole-map volumes, chunks or worker. */
			const bool active;

			struct Chunk {
				int cx, cy, cz;
				float data[ChunkSize][ChunkSize][ChunkSize][2];
				// What map edits left to recompute: written by the render thread,
				// taken by the worker, both under `dirtyMutex`
				bool dirty = true;
				int dirtyMinX = 0, dirtyMaxX = ChunkSize - 1;
				int dirtyMinY = 0, dirtyMaxY = ChunkSize - 1;
				int dirtyMinZ = 0, dirtyMaxZ = ChunkSize - 1;
				// Who owns the data: the worker while true, which then computes
				// into it and hands it over by clearing this; the render thread
				// while false, which copies it out and hands it back by setting it
				std::atomic<bool> transferDone{true};
			};
			std::mutex dirtyMutex;

			int w, h, d;
			int chunkW, chunkH, chunkD;

			std::vector<Chunk> chunks;

			VkImage image;
			VmaAllocation imageAllocation;
			VkImageView imageView;
			VkSampler sampler;

			// Staging, a slot per chunk uploaded (16x16x16 RG float = 32 KB), in
			// a partition of its own for each frame in flight: a frame writes
			// only its own, which the GPU is done with once that frame slot's
			// fence was waited on.
			static constexpr int kStagingSlotsPerFrame = 16;
			VkBuffer stagingBuffer;
			VmaAllocation stagingAllocation;
			void* stagingMapped;
			std::size_t stagingSlotSize;

			UpdateDispatch* dispatch;
			/** Set before the worker is joined for good: it stops at the next chunk
			 * rather than finishing its batch */
			std::atomic<bool> stopping{false};

			Chunk& GetChunk(int cx, int cy, int cz) {
				return chunks[(cx + cy * chunkW) * chunkD + cz];
			}

			Chunk& GetChunkWrapped(int cx, int cy, int cz) {
				return GetChunk(cx & (chunkW - 1), cy & (chunkH - 1), cz);
			}

			void Invalidate(int minX, int minY, int minZ, int maxX, int maxY, int maxZ);

			/** On the worker: recomputes what is dirty in `chunk`, whose data
			 * it owns */
			void UpdateChunk(Chunk& chunk);
			/** On the worker: recomputes a batch of dirty chunks, those around
			 * `eyeChunk` first */
			void UpdateDirtyChunks(IntVector3 eyeChunk);
			/** Dirty chunks the worker can take: not those whose last result is
			 * still waiting to be uploaded */
			int GetNumAvailableChunks();

			float Evaluate(IntVector3);

		public:
			VulkanAmbientShadowRenderer(VulkanRenderer& renderer, client::GameMap& map, bool active);
			~VulkanAmbientShadowRenderer();

			void GameMapChanged(int x, int y, int z, client::GameMap*);

			// Schedule a background recompute (if needed) and upload any chunks
			// the previous dispatch finished, through frame slot `frameSlot`'s
			// staging. Must be called outside a render pass.
			void Update(VkCommandBuffer commandBuffer, std::size_t frameSlot);

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
