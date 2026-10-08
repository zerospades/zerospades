/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <vector>

#include "IGLDevice.h"

namespace spades {
	namespace draw {
		class GLDynamicLight;
		class GLDynamicLightTable;
		class GLProgram;
		class GLRenderer;

		/**
		 * Where the map stops each spotlight's beam, traced once a frame so that the
		 * lighting shaders look it up instead of walking the map for every fragment
		 * (`DynamicLight/Lights.fs`).
		 *
		 * Each spotlight with a map gets a square tile of a shared texture laid over
		 * its image, every texel holding how far the ray through it gets from the
		 * light before it enters a solid block. Tracing a ray per texel costs far
		 * less than a walk per lit fragment once several beams overlap, and it is
		 * traced from this frame's map, so it follows every block built or destroyed.
		 *
		 * The tiles see the map a ray per texel, so a light only gets one where a
		 * texel stays small across its reach; any other light keeps the exact walk.
		 */
		class GLDynamicLightOcclusionMaps {
		public:
			/** The texels along a tile's edge: `DYNAMIC_LIGHT_OCCLUSION_TILE` in the
			 * shaders. */
			static constexpr int TileSize = 128;
			/** The tiles along the texture's edge. */
			static constexpr int TilesPerRow = 8;
			static constexpr int MaxTiles = TilesPerRow * TilesPerRow;
			/** `DYNAMIC_LIGHT_OCCLUSION_ATLAS` in the shaders. */
			static constexpr int AtlasSize = TileSize * TilesPerRow;

			/**
			 * How much wider a texel gets per block away from the light: the tile spans
			 * the light's image out to where its cone fades out.
			 */
			static float GetTexelSpread(const GLDynamicLight&);

			/** Whether a light can have a map: a spotlight whose texels stay small
			 * enough over its whole reach. */
			static bool IsMappable(const GLDynamicLight&);

		private:
			GLRenderer& renderer;
			IGLDevice& device;
			GLProgram* program;
			IGLDevice::UInteger texture;
			IGLDevice::UInteger framebuffer;

		public:
			explicit GLDynamicLightOcclusionMaps(GLRenderer&);
			~GLDynamicLightOcclusionMaps();

			GLDynamicLightOcclusionMaps(const GLDynamicLightOcclusionMaps&) = delete;
			GLDynamicLightOcclusionMaps& operator=(const GLDynamicLightOcclusionMaps&) = delete;

			/** Traces the tiles `table` gave this frame's lights. */
			void Render(const std::vector<GLDynamicLight>&, const GLDynamicLightTable& table);

			IGLDevice::UInteger GetTexture() const { return texture; }
		};
	} // namespace draw
} // namespace spades
