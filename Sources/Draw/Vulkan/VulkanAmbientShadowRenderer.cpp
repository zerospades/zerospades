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

#include "VulkanAmbientShadowRenderer.h"
#include "VulkanRenderer.h"
#include <Client/GameMap.h>
#include <Core/ConcurrentDispatch.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Gui/SDLVulkanDevice.h>
#include <algorithm>
#include <cstring>

namespace spades {
	namespace draw {

		class VulkanAmbientShadowRenderer::UpdateDispatch : public ConcurrentDispatch {
			VulkanAmbientShadowRenderer& owner;

		public:
			std::atomic<bool> done{false};
			explicit UpdateDispatch(VulkanAmbientShadowRenderer& o) : owner(o) {}
			void Run() override {
				SPADES_MARK_FUNCTION();
				owner.UpdateDirtyChunks();
				done = true;
			}
		};

		VulkanAmbientShadowRenderer::VulkanAmbientShadowRenderer(VulkanRenderer& r,
		                                                         client::GameMap& m)
		    : renderer(r),
		      device(r.GetDevice()),
		      map(&m),
		      image(VK_NULL_HANDLE),
		      imageAllocation(VK_NULL_HANDLE),
		      imageView(VK_NULL_HANDLE),
		      sampler(VK_NULL_HANDLE),
		      stagingBuffer(VK_NULL_HANDLE),
		      stagingAllocation(VK_NULL_HANDLE),
		      stagingMapped(nullptr),
		      stagingSlotSize(0),
		      nextStagingSlot(0),
		      dispatch(nullptr) {
			SPADES_MARK_FUNCTION();

			for (auto& rayDir : rays)
				rayDir = RandomUnitVector();

			w = map->Width();
			h = map->Height();
			d = map->Depth();

			chunkW = w / ChunkSize;
			chunkH = h / ChunkSize;
			chunkD = d / ChunkSize;

			chunks = std::vector<Chunk>(static_cast<std::size_t>(chunkW * chunkH * chunkD));
			for (Chunk& c : chunks) {
				float* data = (float*)c.data;
				std::fill(data, data + ChunkSize * ChunkSize * ChunkSize * 2, 1.0F);
			}
			for (int x = 0; x < chunkW; x++)
				for (int y = 0; y < chunkH; y++)
					for (int z = 0; z < chunkD; z++) {
						Chunk& c = GetChunk(x, y, z);
						c.cx = x;
						c.cy = y;
						c.cz = z;
					}

			VkDevice dev = device->GetDevice();
			VmaAllocator alloc = device->GetAllocator();

			// 3D image: w * h * (d + 1), RG float
			VkImageCreateInfo imageInfo{};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_3D;
			imageInfo.format = VK_FORMAT_R32G32_SFLOAT;
			imageInfo.extent = {(uint32_t)w, (uint32_t)h, (uint32_t)(d + 1)};
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			VmaAllocationCreateInfo imageAllocInfo{};
			imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

			if (vmaCreateImage(alloc, &imageInfo, &imageAllocInfo,
			                   &image, &imageAllocation, nullptr) != VK_SUCCESS)
				SPRaise("Failed to create AO 3D image");

			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = image;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
			viewInfo.format = VK_FORMAT_R32G32_SFLOAT;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.layerCount = 1;
			if (vkCreateImageView(dev, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
				SPRaise("Failed to create AO image view");

			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = VK_FILTER_LINEAR;
			samplerInfo.minFilter = VK_FILTER_LINEAR;
			samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			if (vkCreateSampler(dev, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
				SPRaise("Failed to create AO sampler");

			// Staging ring: kStagingSlotCount slots, each holds one ChunkSize^3 RG float region.
			stagingSlotSize = ChunkSize * ChunkSize * ChunkSize * 2 * sizeof(float);

			VkBufferCreateInfo bufInfo{};
			bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufInfo.size = stagingSlotSize * kStagingSlotCount;
			bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo bufAllocInfo{};
			bufAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
			bufAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

			VmaAllocationInfo allocResult{};
			if (vmaCreateBuffer(alloc, &bufInfo, &bufAllocInfo,
			                    &stagingBuffer, &stagingAllocation, &allocResult) != VK_SUCCESS)
				SPRaise("Failed to create AO staging buffer");
			stagingMapped = allocResult.pMappedData;

			// One-time submit: clear the whole 3D image to (1, 1) so the first
			// frames that sample it before any chunk has been uploaded see a
			// neutral "fully lit" value.
			VkCommandBufferAllocateInfo cbAlloc{};
			cbAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cbAlloc.commandPool = device->GetCommandPool();
			cbAlloc.commandBufferCount = 1;
			VkCommandBuffer cb;
			if (vkAllocateCommandBuffers(dev, &cbAlloc, &cb) != VK_SUCCESS)
				SPRaise("Failed to allocate AO init command buffer");

			VkCommandBufferBeginInfo begin{};
			begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(cb, &begin);

			VkImageMemoryBarrier toClear{};
			toClear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toClear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			toClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toClear.image = image;
			toClear.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			toClear.srcAccessMask = 0;
			toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier(cb,
			                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     0, 0, nullptr, 0, nullptr, 1, &toClear);

			VkClearColorValue cv{};
			cv.float32[0] = 1.0F;
			cv.float32[1] = 1.0F;
			cv.float32[2] = 0.0F;
			cv.float32[3] = 0.0F;
			VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			vkCmdClearColorImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                     &cv, 1, &range);

			VkImageMemoryBarrier toRead{};
			toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toRead.image = image;
			toRead.subresourceRange = range;
			toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(cb,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                     0, 0, nullptr, 0, nullptr, 1, &toRead);

			vkEndCommandBuffer(cb);

			VkSubmitInfo submit{};
			submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &cb;
			vkQueueSubmit(device->GetGraphicsQueue(), 1, &submit, VK_NULL_HANDLE);
			vkQueueWaitIdle(device->GetGraphicsQueue());
			vkFreeCommandBuffers(dev, device->GetCommandPool(), 1, &cb);

			SPLog("Vulkan ambient-shadow renderer created (%dx%dx%d)", w, h, d + 1);
		}

		VulkanAmbientShadowRenderer::~VulkanAmbientShadowRenderer() {
			SPADES_MARK_FUNCTION();
			if (dispatch) {
				dispatch->Join();
				delete dispatch;
				dispatch = nullptr;
			}

			VkDevice dev = device->GetDevice();
			VmaAllocator alloc = device->GetAllocator();

			vkDeviceWaitIdle(dev);

			if (sampler) vkDestroySampler(dev, sampler, nullptr);
			if (imageView) vkDestroyImageView(dev, imageView, nullptr);
			if (image) vmaDestroyImage(alloc, image, imageAllocation);
			if (stagingBuffer) vmaDestroyBuffer(alloc, stagingBuffer, stagingAllocation);
		}

		float VulkanAmbientShadowRenderer::Evaluate(IntVector3 ipos) {
			SPADES_MARK_FUNCTION_DEBUG();
			float sum = 0.0F;
			Vector3 pos = MakeVector3(ipos) + 0.5F;

			for (int i = 0; i < NumRays; i++) {
				Vector3 dir = rays[i];
				unsigned int bits = i & 7;
				if (bits & 1) dir.x = -dir.x;
				if (bits & 2) dir.y = -dir.y;
				if (bits & 4) dir.z = -dir.z;

				IntVector3 hitBlock;
				float brightness = 1.0F;
				if (map->CastRay(pos, dir, (float)RayLength, hitBlock)) {
					float dist = ((MakeVector3(hitBlock) + 0.5F) - pos).GetSquaredLength();
					brightness = dist * (1.0F / float((RayLength - 1) * (RayLength - 1)));
					if (brightness > 1.0F)
						brightness = 1.0F;
				}
				sum += brightness;
			}
			return std::min(sum * (2.0F / (float)NumRays), 1.0F);
		}

		void VulkanAmbientShadowRenderer::GameMapChanged(int x, int y, int z, client::GameMap* m) {
			SPADES_MARK_FUNCTION_DEBUG();
			if (m != map.GetPointerOrNull())
				return;
			Invalidate(x - RayLength, y - RayLength, z - RayLength,
			           x + RayLength, y + RayLength, z + RayLength);
		}

		void VulkanAmbientShadowRenderer::Invalidate(int minX, int minY, int minZ,
		                                              int maxX, int maxY, int maxZ) {
			SPADES_MARK_FUNCTION_DEBUG();
			if (minZ < 0) minZ = 0;
			if (maxZ > d - 1) maxZ = d - 1;
			if (minX > maxX || minY > maxY || minZ > maxZ)
				return;

			int cx1 = minX >> ChunkSizeBits;
			int cy1 = minY >> ChunkSizeBits;
			int cz1 = minZ >> ChunkSizeBits;
			int cx2 = maxX >> ChunkSizeBits;
			int cy2 = maxY >> ChunkSizeBits;
			int cz2 = maxZ >> ChunkSizeBits;

			for (int cx = cx1; cx <= cx2; cx++)
				for (int cy = cy1; cy <= cy2; cy++)
					for (int cz = cz1; cz <= cz2; cz++) {
						Chunk& c = GetChunkWrapped(cx, cy, cz);
						int originX = cx * ChunkSize;
						int originY = cy * ChunkSize;
						int originZ = cz * ChunkSize;
						int inMinX = std::max(minX - originX, 0);
						int inMinY = std::max(minY - originY, 0);
						int inMinZ = std::max(minZ - originZ, 0);
						int inMaxX = std::min(maxX - originX, ChunkSize - 1);
						int inMaxY = std::min(maxY - originY, ChunkSize - 1);
						int inMaxZ = std::min(maxZ - originZ, ChunkSize - 1);
						if (!c.dirty) {
							c.dirtyMinX = inMinX;
							c.dirtyMinY = inMinY;
							c.dirtyMinZ = inMinZ;
							c.dirtyMaxX = inMaxX;
							c.dirtyMaxY = inMaxY;
							c.dirtyMaxZ = inMaxZ;
							c.dirty = true;
						} else {
							c.dirtyMinX = std::min(inMinX, c.dirtyMinX);
							c.dirtyMinY = std::min(inMinY, c.dirtyMinY);
							c.dirtyMinZ = std::min(inMinZ, c.dirtyMinZ);
							c.dirtyMaxX = std::max(inMaxX, c.dirtyMaxX);
							c.dirtyMaxY = std::max(inMaxY, c.dirtyMaxY);
							c.dirtyMaxZ = std::max(inMaxZ, c.dirtyMaxZ);
						}
					}
		}

		int VulkanAmbientShadowRenderer::GetNumDirtyChunks() {
			return (int)std::count_if(chunks.begin(), chunks.end(),
			                          [](const Chunk& c) { return c.dirty; });
		}

		void VulkanAmbientShadowRenderer::Update(VkCommandBuffer cmd) {
			SPADES_MARK_FUNCTION();

			if (GetNumDirtyChunks() > 0 && (dispatch == nullptr || dispatch->done)) {
				if (dispatch) {
					dispatch->Join();
					delete dispatch;
				}
				dispatch = new UpdateDispatch(*this);
				dispatch->Start();
			}

			// Walk all chunks once, picking up any whose background recompute
			// finished (transferDone == false). Each gets one staging-slot copy
			// and one CmdCopyBufferToImage region.
			std::vector<VkBufferImageCopy> regions;
			regions.reserve(kStagingSlotCount);

			std::vector<Chunk*> uploaded;
			uploaded.reserve(kStagingSlotCount);

			for (Chunk& c : chunks) {
				if ((int)uploaded.size() >= kStagingSlotCount)
					break;
				if (!c.transferDone.exchange(true)) {
					// Copy this chunk's data into the next staging slot.
					int slot = nextStagingSlot;
					nextStagingSlot = (nextStagingSlot + 1) % kStagingSlotCount;
					std::size_t offset = slot * stagingSlotSize;
					std::memcpy(static_cast<std::uint8_t*>(stagingMapped) + offset,
					            c.data, stagingSlotSize);

					VkBufferImageCopy region{};
					region.bufferOffset = offset;
					region.bufferRowLength = 0;
					region.bufferImageHeight = 0;
					region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					region.imageSubresource.mipLevel = 0;
					region.imageSubresource.baseArrayLayer = 0;
					region.imageSubresource.layerCount = 1;
					region.imageOffset.x = c.cx * ChunkSize;
					region.imageOffset.y = c.cy * ChunkSize;
					region.imageOffset.z = c.cz * ChunkSize + 1; // +1: 0-th slice is the
					                                              //     "below ground" guard.
					region.imageExtent = {ChunkSize, ChunkSize, ChunkSize};
					regions.push_back(region);
					uploaded.push_back(&c);
				}
			}

			if (regions.empty())
				return;

			// Transition image SHADER_READ_ONLY -> TRANSFER_DST, copy, transition back.
			VkImageMemoryBarrier toCopy{};
			toCopy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toCopy.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			toCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toCopy.image = image;
			toCopy.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			toCopy.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			toCopy.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkCmdPipelineBarrier(cmd,
			                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     0, 0, nullptr, 0, nullptr, 1, &toCopy);

			vkCmdCopyBufferToImage(cmd, stagingBuffer, image,
			                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                       (uint32_t)regions.size(), regions.data());

			VkImageMemoryBarrier toRead{};
			toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toRead.image = image;
			toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(cmd,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                     0, 0, nullptr, 0, nullptr, 1, &toRead);
		}

		void VulkanAmbientShadowRenderer::UpdateDirtyChunks() {
			std::array<std::size_t, 256> dirtyChunkIds;
			std::size_t numDirtyChunks = 0;

			const auto& viewOrigin = renderer.GetSceneDef().viewOrigin;
			int eyeX = (int)(viewOrigin.x) >> ChunkSizeBits;
			int eyeY = (int)(viewOrigin.y) >> ChunkSizeBits;
			int eyeZ = (int)(viewOrigin.z) >> ChunkSizeBits;

			for (size_t i = 0; i < chunks.size(); i++) {
				Chunk& c = chunks[i];
				int dx = (c.cx - eyeX) & (chunkW - 1);
				int dy = (c.cy - eyeY) & (chunkH - 1);
				int dz = (c.cz - eyeZ);
				if (dx >= 6 && dx <= chunkW - 6) continue;
				if (dy >= 6 && dy <= chunkW - 6) continue;
				if (dz >= 6 || dz <= -6) continue;
				if (c.dirty) {
					dirtyChunkIds[numDirtyChunks++] = static_cast<int>(i);
					if (numDirtyChunks >= dirtyChunkIds.size()) break;
				}
			}

			if (numDirtyChunks == 0) {
				for (size_t i = 0; i < chunks.size(); i++) {
					Chunk& c = chunks[i];
					if (c.dirty) {
						dirtyChunkIds[numDirtyChunks++] = static_cast<int>(i);
						if (numDirtyChunks >= dirtyChunkIds.size()) break;
					}
				}
			}

			for (int i = 0; i < 8; i++) {
				if (numDirtyChunks <= 0) break;
				std::size_t idx = SampleRandomInt(std::size_t{0}, numDirtyChunks - 1);
				Chunk& c = chunks[dirtyChunkIds[idx]];
				if (idx < numDirtyChunks - 1)
					std::swap(dirtyChunkIds[idx], dirtyChunkIds[numDirtyChunks - 1]);
				numDirtyChunks--;
				UpdateChunk(c.cx, c.cy, c.cz);
			}
		}

		void VulkanAmbientShadowRenderer::UpdateChunk(int cx, int cy, int cz) {
			Chunk& c = GetChunk(cx, cy, cz);
			if (!c.dirty)
				return;

			int originX = cx * ChunkSize;
			int originY = cy * ChunkSize;
			int originZ = cz * ChunkSize;

			constexpr int padding = 2;
			float wData[ChunkSize + padding * 2][ChunkSize + padding * 2][ChunkSize + padding * 2][2];
			std::uint8_t wFlags[ChunkSize + padding * 2][ChunkSize + padding * 2][ChunkSize + padding * 2];
			int wOriginX = originX - padding;
			int wOriginY = originY - padding;
			int wOriginZ = originZ - padding;
			int wDirtyMinX = c.dirtyMinX;
			int wDirtyMinY = c.dirtyMinY;
			int wDirtyMinZ = c.dirtyMinZ;
			int wDirtyMaxX = c.dirtyMaxX + padding * 2;
			int wDirtyMaxY = c.dirtyMaxY + padding * 2;
			int wDirtyMaxZ = c.dirtyMaxZ + padding * 2;

			auto b = [](int i) -> std::uint8_t { return (std::uint8_t)1 << i; };
			auto to_b = [](bool v, int i) -> std::uint8_t { return (std::uint8_t)v << i; };

			for (int z = wDirtyMinZ; z <= wDirtyMaxZ; z++)
				for (int y = wDirtyMinY; y <= wDirtyMaxY; y++)
					for (int x = wDirtyMinX; x <= wDirtyMaxX; x++) {
						IntVector3 pos{x + wOriginX, y + wOriginY, z + wOriginZ};
						if (map->IsSolidWrapped(pos.x, pos.y, pos.z)) {
							wData[z][y][x][0] = 0.0F;
							wData[z][y][x][1] = 0.0F;
						} else {
							wData[z][y][x][0] = Evaluate(pos);
							wData[z][y][x][1] = 1.0F;
						}
						wFlags[z][y][x] =
						    to_b(map->IsSolidWrapped(pos.x, pos.y, pos.z), 0) |
						    to_b(map->IsSolidWrapped(pos.x - 1, pos.y - 1, pos.z - 1) ||
						           map->IsSolidWrapped(pos.x - 1, pos.y - 1, pos.z) ||
						           map->IsSolidWrapped(pos.x - 1, pos.y - 1, pos.z + 1) ||
						           map->IsSolidWrapped(pos.x - 1, pos.y, pos.z - 1) ||
						           map->IsSolidWrapped(pos.x - 1, pos.y, pos.z) ||
						           map->IsSolidWrapped(pos.x - 1, pos.y, pos.z + 1) ||
						           map->IsSolidWrapped(pos.x - 1, pos.y + 1, pos.z - 1) ||
						           map->IsSolidWrapped(pos.x - 1, pos.y + 1, pos.z) ||
						           map->IsSolidWrapped(pos.x - 1, pos.y + 1, pos.z + 1) ||
						           map->IsSolidWrapped(pos.x, pos.y - 1, pos.z - 1) ||
						           map->IsSolidWrapped(pos.x, pos.y - 1, pos.z) ||
						           map->IsSolidWrapped(pos.x, pos.y - 1, pos.z + 1) ||
						           map->IsSolidWrapped(pos.x, pos.y, pos.z - 1) ||
						           map->IsSolidWrapped(pos.x, pos.y, pos.z + 1) ||
						           map->IsSolidWrapped(pos.x, pos.y + 1, pos.z - 1) ||
						           map->IsSolidWrapped(pos.x, pos.y + 1, pos.z) ||
						           map->IsSolidWrapped(pos.x, pos.y + 1, pos.z + 1) ||
						           map->IsSolidWrapped(pos.x + 1, pos.y - 1, pos.z - 1) ||
						           map->IsSolidWrapped(pos.x + 1, pos.y - 1, pos.z) ||
						           map->IsSolidWrapped(pos.x + 1, pos.y - 1, pos.z + 1) ||
						           map->IsSolidWrapped(pos.x + 1, pos.y, pos.z - 1) ||
						           map->IsSolidWrapped(pos.x + 1, pos.y, pos.z) ||
						           map->IsSolidWrapped(pos.x + 1, pos.y, pos.z + 1) ||
						           map->IsSolidWrapped(pos.x + 1, pos.y + 1, pos.z - 1) ||
						           map->IsSolidWrapped(pos.x + 1, pos.y + 1, pos.z) ||
						           map->IsSolidWrapped(pos.x + 1, pos.y + 1, pos.z + 1),
						         1);
					}

			// Compensate for under-shadowing (sampled 0.5 blocks off the surface).
			for (int z = wDirtyMinZ; z <= wDirtyMaxZ; z++)
				for (int y = wDirtyMinY; y <= wDirtyMaxY; y++)
					for (int x = wDirtyMinX; x <= wDirtyMaxX; x++) {
						float& v = wData[z][y][x][0];
						v *= v * v + 1.0F - v;
					}

			// Three-pass separable blur to denoise; only blend across voxels that
			// share the same surface category (see GLAmbientShadowRenderer for the
			// truth table — this matches it bit-for-bit).
			static const float divider[] = {1.0F, 1.0F / 2.0F, 1.0F / 3.0F};
			auto mask = [](bool v, float x) { return v ? x : 0.0F; };
			auto shouldBlur = [=](std::uint8_t thisFlags, std::uint8_t neighborFlags) {
				return ((neighborFlags & b(0)) | ((~thisFlags | neighborFlags) & b(1))) == 0b10;
			};
			for (int blurPass = 0; blurPass < 2; ++blurPass) {
				for (int z = wDirtyMinZ; z <= wDirtyMaxZ; z++)
					for (int y = wDirtyMinY; y <= wDirtyMaxY; y++)
						for (int x = wDirtyMinX + 1; x < wDirtyMaxX; x++) {
							if (wFlags[z][y][x] & b(0)) continue;
							bool m1 = shouldBlur(wFlags[z][y][x], wFlags[z][y][x - 1]);
							bool m2 = shouldBlur(wFlags[z][y][x], wFlags[z][y][x + 1]);
							wData[z][y][x][0] =
							    (wData[z][y][x][0] + mask(m1, wData[z][y][x - 1][0]) +
							     mask(m2, wData[z][y][x + 1][0])) *
							    divider[(int)m1 + (int)m2];
						}
				for (int z = wDirtyMinZ; z <= wDirtyMaxZ; z++)
					for (int y = wDirtyMinY + 1; y < wDirtyMaxY; y++)
						for (int x = wDirtyMinX; x <= wDirtyMaxX; x++) {
							if (wFlags[z][y][x] & b(0)) continue;
							bool m1 = shouldBlur(wFlags[z][y][x], wFlags[z][y - 1][x]);
							bool m2 = shouldBlur(wFlags[z][y][x], wFlags[z][y + 1][x]);
							wData[z][y][x][0] =
							    (wData[z][y][x][0] + mask(m1, wData[z][y - 1][x][0]) +
							     mask(m2, wData[z][y + 1][x][0])) *
							    divider[(int)m1 + (int)m2];
						}
				for (int z = wDirtyMinZ + 1; z < wDirtyMaxZ; z++)
					for (int y = wDirtyMinY; y <= wDirtyMaxY; y++)
						for (int x = wDirtyMinX; x <= wDirtyMaxX; x++) {
							if (wFlags[z][y][x] & b(0)) continue;
							bool m1 = shouldBlur(wFlags[z][y][x], wFlags[z - 1][y][x]);
							bool m2 = shouldBlur(wFlags[z][y][x], wFlags[z + 1][y][x]);
							wData[z][y][x][0] =
							    (wData[z][y][x][0] + mask(m1, wData[z - 1][y][x][0]) +
							     mask(m2, wData[z + 1][y][x][0])) *
							    divider[(int)m1 + (int)m2];
						}
			}

			for (int z = c.dirtyMinZ; z <= c.dirtyMaxZ; z++)
				for (int y = c.dirtyMinY; y <= c.dirtyMaxY; y++)
					for (int x = c.dirtyMinX; x <= c.dirtyMaxX; x++) {
						c.data[z][y][x][0] = wData[z + padding][y + padding][x + padding][0];
						c.data[z][y][x][1] = wData[z + padding][y + padding][x + padding][1];
					}

			c.dirty = false;
			c.transferDone = false;
		}

	} // namespace draw
} // namespace spades
