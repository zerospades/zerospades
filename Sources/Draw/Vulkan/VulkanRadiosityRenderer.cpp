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

#include "VulkanRadiosityRenderer.h"
#include "VulkanMapShadowRenderer.h"
#include "VulkanRenderer.h"
#include <Client/GameMap.h>
#include <Core/ConcurrentDispatch.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Gui/SDLVulkanDevice.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace spades {
	namespace draw {

		class VulkanRadiosityRenderer::UpdateDispatch : public ConcurrentDispatch {
			VulkanRadiosityRenderer& owner;

		public:
			std::atomic<bool> done{false};
			explicit UpdateDispatch(VulkanRadiosityRenderer& o) : owner(o) {}
			void Run() override {
				SPADES_MARK_FUNCTION();
				owner.UpdateDirtyChunks();
				done = true;
			}
		};

		// One pixel of the neutral fill is decoded by the shader to ~0
		// radiosity contribution (each 10-bit channel = 0x200 = ~0.5,
		// which the decoder maps to ~0). Same as GLRadiosityRenderer.
		static constexpr std::uint32_t kNeutralPixel = 0x20080200u;

		VulkanRadiosityRenderer::VulkanRadiosityRenderer(VulkanRenderer& r,
		                                                 client::GameMap& m)
		    : renderer(r),
		      device(r.GetDevice()),
		      map(&m),
		      imageFlat(VK_NULL_HANDLE), imageX(VK_NULL_HANDLE),
		      imageY(VK_NULL_HANDLE), imageZ(VK_NULL_HANDLE),
		      allocFlat(VK_NULL_HANDLE), allocX(VK_NULL_HANDLE),
		      allocY(VK_NULL_HANDLE), allocZ(VK_NULL_HANDLE),
		      viewFlat(VK_NULL_HANDLE), viewX(VK_NULL_HANDLE),
		      viewY(VK_NULL_HANDLE), viewZ(VK_NULL_HANDLE),
		      sampler(VK_NULL_HANDLE),
		      stagingBuffer(VK_NULL_HANDLE),
		      stagingAllocation(VK_NULL_HANDLE),
		      stagingMapped(nullptr),
		      stagingPerTextureSize(0),
		      stagingSlotSize(0),
		      nextStagingSlot(0),
		      dispatch(nullptr) {
			SPADES_MARK_FUNCTION();

			w = map->Width();
			h = map->Height();
			d = map->Depth();

			chunkW = w / ChunkSize;
			chunkH = h / ChunkSize;
			chunkD = d / ChunkSize;

			chunks = std::vector<Chunk>(static_cast<std::size_t>(chunkW * chunkH * chunkD));
			for (Chunk& c : chunks) {
				std::fill(reinterpret_cast<VoxelType*>(c.dataFlat),
				          reinterpret_cast<VoxelType*>(c.dataFlat) + ChunkSize * ChunkSize * ChunkSize,
				          kNeutralPixel);
				std::fill(reinterpret_cast<VoxelType*>(c.dataX),
				          reinterpret_cast<VoxelType*>(c.dataX) + ChunkSize * ChunkSize * ChunkSize,
				          kNeutralPixel);
				std::fill(reinterpret_cast<VoxelType*>(c.dataY),
				          reinterpret_cast<VoxelType*>(c.dataY) + ChunkSize * ChunkSize * ChunkSize,
				          kNeutralPixel);
				std::fill(reinterpret_cast<VoxelType*>(c.dataZ),
				          reinterpret_cast<VoxelType*>(c.dataZ) + ChunkSize * ChunkSize * ChunkSize,
				          kNeutralPixel);
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

			// Four 3D images, A2R10G10B10_UNORM_PACK32. Format choice
			// mirrors GL's BGRA + UNSIGNED_INT_2_10_10_10_REV interpretation:
			// the encoder writes vec.x at bits 0..9 (= B per this layout) so
			// the GL fragment-shader port can sample `.xyz` and read the
			// same values it reads in OpenGL.
			constexpr VkFormat kFormat = VK_FORMAT_A2R10G10B10_UNORM_PACK32;

			VkImage* imgs[4] = {&imageFlat, &imageX, &imageY, &imageZ};
			VmaAllocation* allocs[4] = {&allocFlat, &allocX, &allocY, &allocZ};
			VkImageView* views[4] = {&viewFlat, &viewX, &viewY, &viewZ};

			for (int i = 0; i < 4; i++) {
				VkImageCreateInfo imageInfo{};
				imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				imageInfo.imageType = VK_IMAGE_TYPE_3D;
				imageInfo.format = kFormat;
				imageInfo.extent = {(uint32_t)w, (uint32_t)h, (uint32_t)d};
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
				                   imgs[i], allocs[i], nullptr) != VK_SUCCESS)
					SPRaise("Failed to create radiosity 3D image %d", i);

				VkImageViewCreateInfo viewInfo{};
				viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				viewInfo.image = *imgs[i];
				viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
				viewInfo.format = kFormat;
				viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				viewInfo.subresourceRange.levelCount = 1;
				viewInfo.subresourceRange.layerCount = 1;
				if (vkCreateImageView(dev, &viewInfo, nullptr, views[i]) != VK_SUCCESS)
					SPRaise("Failed to create radiosity image view %d", i);
			}

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
				SPRaise("Failed to create radiosity sampler");

			// Staging ring: each slot fits all four (flat/X/Y/Z) packed-uint32
			// chunks back-to-back. 16 slots × 64 KiB = 1 MiB.
			stagingPerTextureSize = ChunkSize * ChunkSize * ChunkSize * sizeof(VoxelType);
			stagingSlotSize = stagingPerTextureSize * 4;

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
				SPRaise("Failed to create radiosity staging buffer");
			stagingMapped = allocResult.pMappedData;

			// One-time submit: clear all four images to the neutral fill so
			// the first frames before any chunk has been recomputed see ~0
			// radiosity contribution.
			VkCommandBufferAllocateInfo cbAlloc{};
			cbAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cbAlloc.commandPool = device->GetCommandPool();
			cbAlloc.commandBufferCount = 1;
			VkCommandBuffer cb;
			if (vkAllocateCommandBuffers(dev, &cbAlloc, &cb) != VK_SUCCESS)
				SPRaise("Failed to allocate radiosity init command buffer");

			VkCommandBufferBeginInfo begin{};
			begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(cb, &begin);

			VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

			VkImageMemoryBarrier toClear[4]{};
			for (int i = 0; i < 4; i++) {
				toClear[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				toClear[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				toClear[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				toClear[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toClear[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toClear[i].image = *imgs[i];
				toClear[i].subresourceRange = range;
				toClear[i].srcAccessMask = 0;
				toClear[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			}
			vkCmdPipelineBarrier(cb,
			                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     0, 0, nullptr, 0, nullptr, 4, toClear);

			// 0x20080200 = each 10-bit channel set to 0x200 (= 0.5 unorm),
			// alpha 0. The unorm clear value (0.5, 0.5, 0.5, 0) round-trips
			// to that exact bit pattern at 10-bit precision.
			VkClearColorValue cv{};
			cv.float32[0] = 512.0f / 1023.0f;
			cv.float32[1] = 512.0f / 1023.0f;
			cv.float32[2] = 512.0f / 1023.0f;
			cv.float32[3] = 0.0f;
			for (int i = 0; i < 4; i++)
				vkCmdClearColorImage(cb, *imgs[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				                     &cv, 1, &range);

			VkImageMemoryBarrier toRead[4]{};
			for (int i = 0; i < 4; i++) {
				toRead[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				toRead[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				toRead[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				toRead[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toRead[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toRead[i].image = *imgs[i];
				toRead[i].subresourceRange = range;
				toRead[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				toRead[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			}
			vkCmdPipelineBarrier(cb,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                     0, 0, nullptr, 0, nullptr, 4, toRead);

			vkEndCommandBuffer(cb);

			VkSubmitInfo submit{};
			submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &cb;
			vkQueueSubmit(device->GetGraphicsQueue(), 1, &submit, VK_NULL_HANDLE);
			vkQueueWaitIdle(device->GetGraphicsQueue());
			vkFreeCommandBuffers(dev, device->GetCommandPool(), 1, &cb);

			SPLog("Vulkan radiosity renderer created (%dx%dx%d × 4)", w, h, d);
		}

		VulkanRadiosityRenderer::~VulkanRadiosityRenderer() {
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
			VkImageView views[4] = {viewFlat, viewX, viewY, viewZ};
			VkImage imgs[4] = {imageFlat, imageX, imageY, imageZ};
			VmaAllocation allocs[4] = {allocFlat, allocX, allocY, allocZ};
			for (int i = 0; i < 4; i++) {
				if (views[i]) vkDestroyImageView(dev, views[i], nullptr);
				if (imgs[i]) vmaDestroyImage(alloc, imgs[i], allocs[i]);
			}
			if (stagingBuffer) vmaDestroyBuffer(alloc, stagingBuffer, stagingAllocation);
		}

		// Same algorithm as GLRadiosityRenderer::Evaluate. Reads the heightmap
		// shadow column bitmap to find lit surfaces around the sample point and
		// integrates per-direction colored contributions into 4 vec3s (base +
		// per-axis derivative).
		VulkanRadiosityRenderer::Result
		VulkanRadiosityRenderer::Evaluate(IntVector3 ipos) {
			SPADES_MARK_FUNCTION_DEBUG();

			Result result;
			result.base = MakeVector3(0, 0, 0);
			result.x = MakeVector3(0, 0, 0);
			result.y = MakeVector3(0, 0, 0);
			result.z = MakeVector3(0, 0, 0);

			Vector3 pos = MakeVector3(ipos) + 0.5F;

			VulkanMapShadowRenderer* shadowmap = renderer.GetMapShadowRenderer();
			if (!shadowmap)
				return result;
			const std::vector<uint32_t>& bitmap = shadowmap->GetBitmap();
			int centerX = ipos.x;
			int centerY = ipos.y - ipos.z;
			const int yMask = h - 1;
			const int pitch = w;

			for (int x = -Envelope; x <= Envelope; x++) {
				const uint32_t* column = bitmap.data() + ((centerX + x) & (w - 1));
				for (int y = -Envelope; y <= Envelope; y++) {
					uint32_t pixel = column[pitch * ((centerY + y) & yMask)];
					int depth = pixel >> 24;

					int wx = centerX + x;
					int wy = centerY + y + depth;
					int wz = depth;

					bool isSide = (pixel & 0x80) != 0;

					Vector3 center;
					Vector3 diff;
					float diffDot;
					if (isSide) {
						if (wy <= ipos.y)
							continue;
						center.x = wx + 0.5F;
						center.y = (float)wy;
						center.z = wz - 0.5F;
						diff = pos - center;
						diffDot = -diff.y;
					} else {
						if (wz <= ipos.z)
							continue;
						center.x = wx + 0.5F;
						center.y = wy + 0.5F;
						center.z = (float)wz;
						diff = pos - center;
						diffDot = -diff.z;
					}

					SPAssert(diffDot >= 0.0F);

					float diffLen = diff.GetLength();
					float invDiffLen = 1.0F / diffLen;
					float invDiffLenSmooth = 1.0F / (diffLen + 0.4F);

					float intensity = diffDot * invDiffLen;
					intensity *= invDiffLenSmooth;
					intensity *= invDiffLenSmooth;

					Vector3 normDiff = diff * -invDiffLen;

					float red = static_cast<float>((pixel) & 0x3F);
					float green = static_cast<float>((pixel >> 8) & 0x3F);
					float blue = static_cast<float>((pixel >> 16) & 0x3F);

					Vector3 color = {red, green, blue};
					color *= intensity;

					result.base += color;
					result.x += color * normDiff.x;
					result.y += color * normDiff.y;
					result.z += color * normDiff.z;
				}
			}

			float scale = 0.1F / 64.0F;
			result.base *= scale;
			result.x *= scale;
			result.y *= scale;
			result.z *= scale;

			return result;
		}

		void VulkanRadiosityRenderer::GameMapChanged(int x, int y, int z, client::GameMap* m) {
			SPADES_MARK_FUNCTION_DEBUG();
			if (m != map.GetPointerOrNull())
				return;
			Invalidate(x - Envelope, y - Envelope, z - Envelope,
			           x + Envelope, y + Envelope, z + Envelope);
		}

		void VulkanRadiosityRenderer::Invalidate(int minX, int minY, int minZ,
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

		int VulkanRadiosityRenderer::GetNumDirtyChunks() {
			return (int)std::count_if(chunks.begin(), chunks.end(),
			                          [](const Chunk& c) { return c.dirty; });
		}

		void VulkanRadiosityRenderer::Update(VkCommandBuffer cmd) {
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
			// finished. Each gets one staging-slot copy and four
			// VkBufferImageCopy regions (one per dst texture).
			std::vector<VkBufferImageCopy> regionsFlat, regionsX, regionsY, regionsZ;
			regionsFlat.reserve(kStagingSlotCount);
			regionsX.reserve(kStagingSlotCount);
			regionsY.reserve(kStagingSlotCount);
			regionsZ.reserve(kStagingSlotCount);

			std::vector<Chunk*> uploaded;
			uploaded.reserve(kStagingSlotCount);

			for (Chunk& c : chunks) {
				if ((int)uploaded.size() >= kStagingSlotCount)
					break;
				if (!c.transferDone.exchange(true)) {
					int slot = nextStagingSlot;
					nextStagingSlot = (nextStagingSlot + 1) % kStagingSlotCount;
					std::size_t slotBase = slot * stagingSlotSize;

					std::uint8_t* base =
					    static_cast<std::uint8_t*>(stagingMapped) + slotBase;
					std::memcpy(base + 0 * stagingPerTextureSize,
					            c.dataFlat, stagingPerTextureSize);
					std::memcpy(base + 1 * stagingPerTextureSize,
					            c.dataX, stagingPerTextureSize);
					std::memcpy(base + 2 * stagingPerTextureSize,
					            c.dataY, stagingPerTextureSize);
					std::memcpy(base + 3 * stagingPerTextureSize,
					            c.dataZ, stagingPerTextureSize);

					auto makeRegion = [&](std::size_t subOffset) {
						VkBufferImageCopy r{};
						r.bufferOffset = slotBase + subOffset;
						r.bufferRowLength = 0;
						r.bufferImageHeight = 0;
						r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						r.imageSubresource.mipLevel = 0;
						r.imageSubresource.baseArrayLayer = 0;
						r.imageSubresource.layerCount = 1;
						r.imageOffset.x = c.cx * ChunkSize;
						r.imageOffset.y = c.cy * ChunkSize;
						r.imageOffset.z = c.cz * ChunkSize;
						r.imageExtent = {ChunkSize, ChunkSize, ChunkSize};
						return r;
					};
					regionsFlat.push_back(makeRegion(0 * stagingPerTextureSize));
					regionsX.push_back(makeRegion(1 * stagingPerTextureSize));
					regionsY.push_back(makeRegion(2 * stagingPerTextureSize));
					regionsZ.push_back(makeRegion(3 * stagingPerTextureSize));
					uploaded.push_back(&c);
				}
			}

			if (regionsFlat.empty())
				return;

			VkImage imgs[4] = {imageFlat, imageX, imageY, imageZ};

			VkImageMemoryBarrier toCopy[4]{};
			for (int i = 0; i < 4; i++) {
				toCopy[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				toCopy[i].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				toCopy[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				toCopy[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toCopy[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toCopy[i].image = imgs[i];
				toCopy[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
				toCopy[i].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				toCopy[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			}
			vkCmdPipelineBarrier(cmd,
			                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     0, 0, nullptr, 0, nullptr, 4, toCopy);

			vkCmdCopyBufferToImage(cmd, stagingBuffer, imageFlat,
			                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                       (uint32_t)regionsFlat.size(), regionsFlat.data());
			vkCmdCopyBufferToImage(cmd, stagingBuffer, imageX,
			                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                       (uint32_t)regionsX.size(), regionsX.data());
			vkCmdCopyBufferToImage(cmd, stagingBuffer, imageY,
			                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                       (uint32_t)regionsY.size(), regionsY.data());
			vkCmdCopyBufferToImage(cmd, stagingBuffer, imageZ,
			                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                       (uint32_t)regionsZ.size(), regionsZ.data());

			VkImageMemoryBarrier toRead[4]{};
			for (int i = 0; i < 4; i++) {
				toRead[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				toRead[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				toRead[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				toRead[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toRead[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				toRead[i].image = imgs[i];
				toRead[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
				toRead[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				toRead[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			}
			vkCmdPipelineBarrier(cmd,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                     0, 0, nullptr, 0, nullptr, 4, toRead);
		}

		void VulkanRadiosityRenderer::UpdateDirtyChunks() {
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
					dirtyChunkIds[numDirtyChunks++] = i;
					if (numDirtyChunks >= dirtyChunkIds.size()) break;
				}
			}

			if (numDirtyChunks == 0) {
				for (size_t i = 0; i < chunks.size(); i++) {
					Chunk& c = chunks[i];
					if (c.dirty) {
						dirtyChunkIds[numDirtyChunks++] = i;
						if (numDirtyChunks >= dirtyChunkIds.size()) break;
					}
				}
			}

			// GL processes 8 chunks per dispatch and the menu/post-spawn
			// scene visibly brightens for ~10 s as the radiosity texture
			// converges. We bump this to 128 per dispatch (still on the
			// background thread, so it doesn't block rendering) to bring
			// convergence under 1 s, eliminating the "scene changes while
			// I'm standing still" artefact.
			for (int i = 0; i < 128; i++) {
				if (numDirtyChunks <= 0) break;
				std::size_t idx = SampleRandomInt(std::size_t{0}, numDirtyChunks - 1);
				Chunk& c = chunks[dirtyChunkIds[idx]];
				if (idx < numDirtyChunks - 1)
					std::swap(dirtyChunkIds[idx], dirtyChunkIds[numDirtyChunks - 1]);
				numDirtyChunks--;
				UpdateChunk(c.cx, c.cy, c.cz);
			}
		}

		float VulkanRadiosityRenderer::CompressDynamicRange(float v) {
			// We always use the 10-10-10-2 format, so the GL "r_radiosity >= 2"
			// path is the only one we mirror — pass-through.
			return v;
		}

		std::uint32_t VulkanRadiosityRenderer::EncodeValue(Vector3 vec) {
			// Same packed layout as GLRadiosityRenderer::EncodeValue. With the
			// A2R10G10B10 format choice above, vec.x lands in bits 0..9 (= B
			// channel under this layout), matching GL's BGRA + 2_10_10_10_REV
			// reorder bit-for-bit.
			float v;
			int iv;
			unsigned int out = 0xC0000000u;

			vec.x = CompressDynamicRange(vec.x);
			vec.y = CompressDynamicRange(vec.y);
			vec.z = CompressDynamicRange(vec.z);

			vec *= 0.5F;
			vec += 0.5F;
			vec *= 1022.0F / 1023.0F;

			v = vec.x * 1023.0F + 0.5F;
			if (v > 1023.2F) v = 1023.2F;
			if (v < 0.0F) v = 0.0F;
			iv = (unsigned int)v;
			if (iv > 1023) iv = 1023;
			if (iv < 0) iv = 0;
			out |= iv << 20;

			v = vec.y * 1023.0F + 0.5F;
			if (v > 1023.2F) v = 1023.2F;
			if (v < 0.0F) v = 0.0F;
			iv = (unsigned int)v;
			if (iv > 1023) iv = 1023;
			if (iv < 0) iv = 0;
			out |= iv << 10;

			v = vec.z * 1023.0F + 0.5F;
			if (v > 1023.2F) v = 1023.2F;
			if (v < 0.0F) v = 0.0F;
			iv = (unsigned int)v;
			if (iv > 1023) iv = 1023;
			if (iv < 0) iv = 0;
			out |= iv;

			return out;
		}

		void VulkanRadiosityRenderer::UpdateChunk(int cx, int cy, int cz) {
			Chunk& c = GetChunk(cx, cy, cz);
			if (!c.dirty)
				return;

			int originX = cx * ChunkSize;
			int originY = cy * ChunkSize;
			int originZ = cz * ChunkSize;

			for (int z = c.dirtyMinZ; z <= c.dirtyMaxZ; z++)
				for (int y = c.dirtyMinY; y <= c.dirtyMaxY; y++)
					for (int x = c.dirtyMinX; x <= c.dirtyMaxX; x++) {
						IntVector3 pos;
						pos.x = (x + originX);
						pos.y = (y + originY);
						pos.z = (z + originZ);

						Result res = Evaluate(pos);
						c.dataFlat[z][y][x] = EncodeValue(res.base);
						c.dataX[z][y][x] = EncodeValue(res.x);
						c.dataY[z][y][x] = EncodeValue(res.y);
						c.dataZ[z][y][x] = EncodeValue(res.z);
					}

			c.dirty = false;
			c.transferDone = false;
		}

	} // namespace draw
} // namespace spades
