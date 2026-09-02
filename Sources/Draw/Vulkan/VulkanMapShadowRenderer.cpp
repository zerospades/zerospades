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

#include "VulkanMapShadowRenderer.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include "VulkanBuffer.h"
#include <Client/GameMap.h>
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <cstring>

namespace spades {
	namespace draw {

		static uint32_t BuildPixel(int distance, uint32_t color, bool side) {
			int r = (uint8_t)(color);
			int g = (uint8_t)(color >> 8);
			int b = (uint8_t)(color >> 16);

			r >>= 2;
			g >>= 2;
			b >>= 2;

			int ex1 = side ? 1 : 0, ex2 = 0, ex3 = 0;

			return r + (g << 8) + (b << 16) + (distance << 24) +
			       (ex1 << 7) + (ex2 << 15) + (ex3 << 23);
		}

		VulkanMapShadowRenderer::VulkanMapShadowRenderer(VulkanRenderer& renderer,
		                                                 client::GameMap* map)
		    : renderer(renderer), device(renderer.GetDevice()), map(map),
		      needsFullUpload(true) {
			SPADES_MARK_FUNCTION();

			w = map->Width();
			h = map->Height();
			d = map->Depth();

			updateBitmapPitch = (w + 31) / 32;
			updateBitmap.resize(updateBitmapPitch * h);
			bitmap.resize(w * h);

			// Mark all pixels for initial generation
			std::fill(updateBitmap.begin(), updateBitmap.end(), 0xffffffffUL);
			std::fill(bitmap.begin(), bitmap.end(), 0xffffffffUL);

			// Create shadow image (512x512 RGBA8)
			shadowImage = Handle<VulkanImage>::New(
				device, (uint32_t)w, (uint32_t)h,
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			shadowImage->CreateSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST,
			                           VK_SAMPLER_ADDRESS_MODE_REPEAT, false);

			// Create staging buffer (512*512*4 = 1MB)
			size_t bufferSize = w * h * 4;
			stagingBuffer = Handle<VulkanBuffer>::New(
				device, bufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			// Coarse min/max shadow companion (64×64 for a 512×512 fine map).
			int cw = w >> CoarseBits;
			int ch = h >> CoarseBits;
			coarseBitmap.resize(cw * ch);
			coarseUpdateBitmap.resize(cw * ch, 1u);

			coarseShadowImage = Handle<VulkanImage>::New(
				device, (uint32_t)cw, (uint32_t)ch,
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			coarseShadowImage->CreateSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST,
			                                 VK_SAMPLER_ADDRESS_MODE_REPEAT, false);

			coarseStagingBuffer = Handle<VulkanBuffer>::New(
				device, (size_t)cw * ch * 4,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			// Generate all pixels immediately
			for (int y = 0; y < h; y++) {
				for (int x = 0; x < w; x++) {
					bitmap[x + y * w] = GeneratePixel(x, y);
				}
			}
			std::fill(updateBitmap.begin(), updateBitmap.end(), 0);

			// Build the coarse min/max texture from the fresh fine bitmap.
			for (int cy = 0; cy < ch; cy++) {
				for (int cx = 0; cx < cw; cx++) {
					RegenerateCoarseCell(cx, cy);
				}
			}
			std::fill(coarseUpdateBitmap.begin(), coarseUpdateBitmap.end(), 0u);

			// Upload initial data via one-time command buffer
			stagingBuffer->UpdateData(bitmap.data(), w * h * 4);
			coarseStagingBuffer->UpdateData(coarseBitmap.data(), (size_t)cw * ch * 4);

			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = device->GetCommandPool();
			allocInfo.commandBufferCount = 1;

			VkCommandBuffer commandBuffer;
			if (vkAllocateCommandBuffers(device->GetDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS) {
				SPRaise("Failed to allocate command buffer for shadow map upload");
			}

			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(commandBuffer, &beginInfo);

			shadowImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				0, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

			shadowImage->CopyFromBuffer(commandBuffer, stagingBuffer->GetBuffer());

			shadowImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

			coarseShadowImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				0, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			coarseShadowImage->CopyFromBuffer(commandBuffer, coarseStagingBuffer->GetBuffer());
			coarseShadowImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

			vkEndCommandBuffer(commandBuffer);

			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
			vkQueueWaitIdle(device->GetGraphicsQueue());

			vkFreeCommandBuffers(device->GetDevice(), device->GetCommandPool(), 1, &commandBuffer);

			needsFullUpload = false;

			SPLog("Map shadow renderer created (%dx%d)", w, h);
		}

		VulkanMapShadowRenderer::~VulkanMapShadowRenderer() {
			SPADES_MARK_FUNCTION();
		}

		uint32_t VulkanMapShadowRenderer::GeneratePixel(int x, int y) {
			for (int z = 0; z < d; z++) {
				// z-plane hit
				if (map->IsSolid(x, y, z) && z < 63) {
					return BuildPixel(z, map->GetColor(x, y, z), false);
				}

				y = y + 1;
				if (y == h)
					y = 0;

				// y-plane hit
				if (map->IsSolid(x, y, z) && z < 63) {
					return BuildPixel(z + 1, map->GetColor(x, y, z), true);
				}
			}
			return BuildPixel(64, map->GetColor(x, y == h ? 0 : y, 63), false);
		}

		void VulkanMapShadowRenderer::MarkUpdate(int x, int y) {
			x &= w - 1;
			y &= h - 1;
			updateBitmap[(x >> 5) + y * updateBitmapPitch] |= 1UL << (x & 31);
			// Coarse companion needs to be re-derived for the cell that
			// owns this fine texel.
			int cw = w >> CoarseBits;
			int cy = y >> CoarseBits;
			int cx = x >> CoarseBits;
			coarseUpdateBitmap[cx + cy * cw] = 1u;
		}

		void VulkanMapShadowRenderer::GameMapChanged(int x, int y, int z, client::GameMap* m) {
			MarkUpdate(x, y - z);
			MarkUpdate(x, y - z - 1);
		}

		void VulkanMapShadowRenderer::RegenerateCoarseCell(int cx, int cy) {
			int cw = w >> CoarseBits;
			int x0 = cx << CoarseBits;
			int y0 = cy << CoarseBits;
			int minDepth = -1, maxDepth = 0;
			for (int yy = 0; yy < CoarseSize; yy++) {
				const uint32_t* row = bitmap.data() + (y0 + yy) * w + x0;
				for (int xx = 0; xx < CoarseSize; xx++) {
					int depth = (int)(row[xx] >> 24);
					if (minDepth < 0) {
						minDepth = maxDepth = depth;
					} else {
						if (depth < minDepth) minDepth = depth;
						if (depth > maxDepth) maxDepth = depth;
					}
				}
			}
			// GL packs (min << 16) | (max << 8) and uploads as BGRA, so the
			// fragment shader sees (R, G) = (min, max) / 255. Match that:
			// for an R8G8B8A8_UNORM upload, write byte-0 = min, byte-1 = max,
			// then sample as (.r, .g) = (min, max).
			uint32_t packed = ((uint32_t)minDepth & 0xff) |
			                  (((uint32_t)maxDepth & 0xff) << 8);
			coarseBitmap[cx + cy * cw] = packed;
		}

		void VulkanMapShadowRenderer::Update(VkCommandBuffer commandBuffer) {
			SPADES_MARK_FUNCTION();

			// Dirty 32-texel spans, coalesced per row; adjacent dirty words in
			// the same row merge into one copy region.
			struct Span {
				int x, y, width;
			};
			std::vector<Span> spans;

			for (size_t i = 0; i < updateBitmap.size(); i++) {
				if (updateBitmap[i] == 0)
					continue;

				int y = static_cast<int>(i / updateBitmapPitch);
				int x = static_cast<int>((i - y * updateBitmapPitch) * 32);
				size_t bitmapPixelPosBase = i * 32;

				bool wordChanged = false;
				for (int j = 0; j < 32; j++) {
					uint32_t pixel = GeneratePixel(x + j, y);
					if (bitmap[bitmapPixelPosBase + j] != pixel) {
						bitmap[bitmapPixelPosBase + j] = pixel;
						wordChanged = true;
					}
				}

				updateBitmap[i] = 0;

				if (wordChanged) {
					if (!spans.empty() && spans.back().y == y &&
					    spans.back().x + spans.back().width == x) {
						spans.back().width += 32;
					} else {
						spans.push_back({x, y, 32});
					}
				}
			}

			if (spans.empty())
				return;

			size_t dirtyTexels = 0;
			for (const Span& s : spans)
				dirtyTexels += (size_t)s.width;

			shadowImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

			// Heavy edits: one full upload beats hundreds of tiny copies.
			if (dirtyTexels * 4 >= (size_t)w * h || spans.size() > 256) {
				stagingBuffer->UpdateData(bitmap.data(), w * h * 4);
				shadowImage->CopyFromBuffer(commandBuffer, stagingBuffer->GetBuffer());
			} else {
				// Pack dirty spans contiguously into staging, one copy region each.
				std::vector<VkBufferImageCopy> regions;
				regions.reserve(spans.size());
				VkDeviceSize offset = 0;
				char* mapped = static_cast<char*>(stagingBuffer->Map());
				for (const Span& s : spans) {
					std::memcpy(mapped + offset, bitmap.data() + s.y * w + s.x,
					            (size_t)s.width * 4);

					VkBufferImageCopy region{};
					region.bufferOffset = offset;
					region.bufferRowLength = 0;
					region.bufferImageHeight = 0;
					region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					region.imageSubresource.mipLevel = 0;
					region.imageSubresource.baseArrayLayer = 0;
					region.imageSubresource.layerCount = 1;
					region.imageOffset = {s.x, s.y, 0};
					region.imageExtent = {(uint32_t)s.width, 1, 1};
					regions.push_back(region);

					offset += (VkDeviceSize)s.width * 4;
				}
				stagingBuffer->Unmap();
				vkCmdCopyBufferToImage(commandBuffer, stagingBuffer->GetBuffer(),
				                       shadowImage->GetImage(),
				                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				                       (uint32_t)regions.size(), regions.data());
			}

			shadowImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

			// Refresh the affected coarse cells and re-upload the coarse map.
			int cw = w >> CoarseBits;
			int ch = h >> CoarseBits;
			bool coarseDirty = false;
			for (int cy = 0; cy < ch; cy++) {
				for (int cx = 0; cx < cw; cx++) {
					if (!coarseUpdateBitmap[cx + cy * cw])
						continue;
					RegenerateCoarseCell(cx, cy);
					coarseUpdateBitmap[cx + cy * cw] = 0u;
					coarseDirty = true;
				}
			}
			if (coarseDirty) {
				coarseStagingBuffer->UpdateData(coarseBitmap.data(), (size_t)cw * ch * 4);
				coarseShadowImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
				coarseShadowImage->CopyFromBuffer(commandBuffer, coarseStagingBuffer->GetBuffer());
				coarseShadowImage->TransitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
			}
		}

	} // namespace draw
} // namespace spades
