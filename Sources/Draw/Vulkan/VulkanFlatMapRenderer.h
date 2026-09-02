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

#include <vector>

#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	class Bitmap;
	namespace client {
		class GameMap;
		class IImage;
	}
	namespace draw {
		class VulkanRenderer;
		class VulkanFlatMapRenderer {
			enum { ChunkSize = 16, ChunkBits = 4 };

			VulkanRenderer& renderer;
			Handle<client::GameMap> map;
			std::vector<bool> chunkInvalid;

			Handle<client::IImage> image;

			int chunkCols, chunkRows;

			Bitmap* GenerateBitmap(int x, int y, int w, int h);

		public:
			VulkanFlatMapRenderer(VulkanRenderer& renderer, client::GameMap& map);
			~VulkanFlatMapRenderer();
			void Draw(const AABB2& dest, const AABB2& src);
			void Draw(const Vector2& destTopLeft, const Vector2& destTopRight,
			          const Vector2& destBottomLeft, const AABB2& src);
			void UpdateChunks();
			void GameMapChanged(int x, int y, int z, client::GameMap&);
		};
	} // namespace draw
} // namespace spades
