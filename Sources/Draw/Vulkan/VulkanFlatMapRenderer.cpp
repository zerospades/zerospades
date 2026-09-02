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

#include "VulkanFlatMapRenderer.h"
#include "VulkanRenderer.h"
#include "VulkanImage.h"
#include "VulkanImageWrapper.h"
#include <Gui/SDLVulkanDevice.h>
#include <Client/GameMap.h>
#include <Client/IImage.h>
#include <Core/Bitmap.h>
#include <Core/Debug.h>

namespace spades {
	namespace draw {
		VulkanFlatMapRenderer::VulkanFlatMapRenderer(VulkanRenderer& r, client::GameMap& m)
		    : renderer(r), map(m) {
			SPADES_MARK_FUNCTION();

			chunkRows = m.Height() >> ChunkBits;
			chunkCols = m.Width() >> ChunkBits;
			for (int i = 0; i < chunkRows * chunkCols; i++)
				chunkInvalid.push_back(false);

			Handle<Bitmap> bmp(GenerateBitmap(0, 0, m.Width(), m.Height()), false);
			image = renderer.CreateImage(*bmp);

			auto* wrapper = dynamic_cast<VulkanImageWrapper*>(image.GetPointerOrNull());
			if (wrapper && wrapper->GetVulkanImage()) {
				wrapper->GetVulkanImage()->CreateSampler(
					VK_FILTER_NEAREST, VK_FILTER_NEAREST,
					VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false);
			}
		}

		VulkanFlatMapRenderer::~VulkanFlatMapRenderer() {}

		Bitmap* VulkanFlatMapRenderer::GenerateBitmap(int mx, int my, int w, int h) {
			SPADES_MARK_FUNCTION();
			auto bmp = Handle<Bitmap>::New(w, h);
			try {
				uint32_t* pixels = bmp->GetPixels();

				for (int y = 0; y < h; y++) {
					for (int x = 0; x < w; x++) {
						int px = mx + x, py = my + y;
						for (int z = 0; z < 64; z++) {
							if (map->IsSolid(px, py, z)) {
								uint32_t col = map->GetColor(px, py, z);
								col |= 0xFF000000UL;
								*pixels = col;
								break;
							}
						}
						pixels++;
					}
				}
			} catch (...) {
				throw;
			}
			return std::move(bmp).Unmanage();
		}

		void VulkanFlatMapRenderer::GameMapChanged(int x, int y, int z, client::GameMap& map) {
			if (this->map.GetPointerOrNull() != &map)
				return;

			SPAssert(x >= 0);
			SPAssert(x < map.Width());
			SPAssert(y >= 0);
			SPAssert(y < map.Height());
			SPAssert(z >= 0);
			SPAssert(z < map.Depth());

			int chunkX = x >> ChunkBits;
			int chunkY = y >> ChunkBits;
			int chunkId = chunkX + chunkY * chunkCols;
			SPAssert(chunkId >= 0);
			SPAssert(chunkId < chunkCols * chunkRows);
			chunkInvalid[chunkId] = true;
		}

		void VulkanFlatMapRenderer::UpdateChunks() {
			for (size_t i = 0; i < chunkInvalid.size(); i++) {
				if (!chunkInvalid[i])
					continue;

				int chunkX = ((int)i) % chunkCols;
				int chunkY = ((int)i) / chunkCols;

				Handle<Bitmap> bmp(GenerateBitmap(chunkX * ChunkSize,
					chunkY * ChunkSize, ChunkSize, ChunkSize), false);
				try {
					image->Update(*bmp, chunkX * ChunkSize, chunkY * ChunkSize);
				} catch (...) {
					throw;
				}
				chunkInvalid[i] = false;
			}
		}

		void VulkanFlatMapRenderer::Draw(const AABB2& dest, const AABB2& src) {
			SPADES_MARK_FUNCTION();

			renderer.DrawImage(*image, dest, src);
		}

		void VulkanFlatMapRenderer::Draw(const Vector2& destTopLeft, const Vector2& destTopRight,
		                                 const Vector2& destBottomLeft, const AABB2& src) {
			SPADES_MARK_FUNCTION();

			renderer.DrawImage(*image, destTopLeft, destTopRight, destBottomLeft, src);
		}
	} // namespace draw
} // namespace spades
