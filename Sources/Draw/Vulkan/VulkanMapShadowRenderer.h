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

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>
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
		class VulkanImage;
		class VulkanBuffer;

		/** Generates a heightmap shadow texture from the game map (matching GLMapShadowRenderer). */
		class VulkanMapShadowRenderer {
			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;
			client::GameMap* map;

			Handle<VulkanImage> shadowImage;
			Handle<VulkanBuffer> stagingBuffer;

			// 8×8 downsampled min/max shadow map (matches GLMapShadowRenderer's
			// coarseTexture). For each 8×8 cell of the fine shadow map, stores
			//   .x = minimum first-solid depth in the cell
			//   .y = maximum first-solid depth in the cell
			// (channels 0..255 / 255). The volumetric fog filter's coarse+fine
			// DDA uses this to skip cells that are fully lit or fully shadowed
			// without doing a per-voxel shadow lookup.
			static constexpr int CoarseBits = 3;            // 1 << 3 = 8
			static constexpr int CoarseSize = 1 << CoarseBits;
			Handle<VulkanImage> coarseShadowImage;
			Handle<VulkanBuffer> coarseStagingBuffer;
			std::vector<uint32_t> coarseBitmap;
			std::vector<uint32_t> coarseUpdateBitmap;

			int w, h, d;

			size_t updateBitmapPitch;
			std::vector<uint32_t> updateBitmap;
			std::vector<uint32_t> bitmap;

			bool needsFullUpload;

			uint32_t GeneratePixel(int x, int y);
			void MarkUpdate(int x, int y);
			void RegenerateCoarseCell(int cx, int cy);

		public:
			VulkanMapShadowRenderer(VulkanRenderer& renderer, client::GameMap* map);
			~VulkanMapShadowRenderer();

			void GameMapChanged(int x, int y, int z, client::GameMap*);
			void Update(VkCommandBuffer commandBuffer);

			VulkanImage* GetShadowImage() { return shadowImage.GetPointerOrNull(); }
			VulkanImage* GetCoarseShadowImage() { return coarseShadowImage.GetPointerOrNull(); }

			// Read access to the CPU-side shadow column bitmap. Each pixel
			// is encoded as in GLMapShadowRenderer: alpha=depth(0..63), low
			// bit 7 of the BGR triple flags side-faces vs top-faces, RGB =
			// 6-bit color of the surface block. Used by the radiosity
			// evaluator.
			const std::vector<uint32_t>& GetBitmap() const { return bitmap; }
			int GetWidth() const { return w; }
			int GetHeight() const { return h; }
		};
	} // namespace draw
} // namespace spades
