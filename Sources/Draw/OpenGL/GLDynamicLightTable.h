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

#include <cstddef>
#include <vector>

#include "IGLDevice.h"

namespace spades {
	namespace draw {
		class GLDynamicLight;

		/**
		 * The scene's dynamic lights, uploaded once a frame as a texture that the
		 * lighting shaders look them up in, one row per light (`DynamicLight/Lights.fs`).
		 *
		 * A draw then only names the rows of the lights it takes, instead of sending
		 * each light's parameters again for every chunk and model it reaches.
		 */
		class GLDynamicLightTable {
		public:
			/** The texels a light takes up in its row; the shaders read the same layout. */
			static constexpr int TexelsPerLight = 8;

		private:
			IGLDevice& device;
			IGLDevice::UInteger texture;

			/** Rows the texture has room for. */
			int capacity = 0;

			/** The lights the table was last filled from. */
			const GLDynamicLight* lights = nullptr;
			std::size_t numLights = 0;

			std::vector<float> texels;

			/** Each light's tile of `GLDynamicLightOcclusionMaps`, or `-1`. */
			std::vector<int> tiles;
			int numTiles = 0;

		public:
			explicit GLDynamicLightTable(IGLDevice&);
			~GLDynamicLightTable();

			GLDynamicLightTable(const GLDynamicLightTable&) = delete;
			GLDynamicLightTable& operator=(const GLDynamicLightTable&) = delete;

			/**
			 * Fills the table from this frame's lights, which must stay where they are
			 * until the next call. With `withOcclusionMaps`, the first lights that can
			 * have an occlusion map get its tiles.
			 */
			void Update(const std::vector<GLDynamicLight>&, bool withOcclusionMaps);

			/** The row of `light`, one of the lights the table was last filled from. */
			int GetRow(const GLDynamicLight& light) const;

			/** The occlusion map tile of the light in `row`, or `-1` for none. */
			int GetTile(std::size_t row) const { return tiles[row]; }
			int GetNumTiles() const { return numTiles; }

			IGLDevice::UInteger GetTexture() const { return texture; }

			/** The rows the texture has, which its coordinates are divided by. */
			int GetCapacity() const { return capacity; }
		};
	} // namespace draw
} // namespace spades
