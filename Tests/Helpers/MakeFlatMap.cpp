/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

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

#include "MakeFlatMap.h"

#include <cstdint>
#include <vector>

namespace spades {
	namespace tests {

		std::vector<uint8_t> MakeFlatMapBytes() {
			// VOXLAP5 format: 512x512 columns, y-outer x-inner.
			// Each column: one span (number_4byte_chunks=0) with ground at z=62.
			//
			// Span header (4 bytes):
			//   [0] = number_4byte_chunks = 0  (last/only span)
			//   [1] = top_color_start = 62     (first solid z)
			//   [2] = top_color_end = 62        (inclusive, matches GameMap::Load)
			//   [3] = air_start = 0             (all z < 62 are air)
			// Color (4 bytes): R, G, B, A written in the order GameMap::Save uses:
			//   WriteColor: (color>>16)=R, (color>>8)=G, (color>>0)=B, (color>>24)=A
			// GameMap::Load reads these 4 bytes as a little-endian uint32_t and calls
			// swapColorMap() which swaps R and B, then forces health=100.
			// The exact post-load color is unimportant for determinism tests.
			// We write: R=0x50, G=0x60, B=0x50, A=0x64 (alpha/health=100=0x64)
			// which means color = (A<<24)|(R<<16)|(G<<8)|B = 0x64506050
			// Stream bytes: R=0x50, G=0x60, B=0x50, A=0x64

			constexpr int W = 512;
			constexpr int H = 512;
			constexpr int GROUND_Z = 62;
			constexpr uint8_t COLOR_R = 0x50;
			constexpr uint8_t COLOR_G = 0x60;
			constexpr uint8_t COLOR_B = 0x50;
			constexpr uint8_t COLOR_A = 0x64; // health = 100

			std::vector<uint8_t> buf;
			buf.reserve(W * H * 8); // exactly 2,097,152 bytes

			for (int y = 0; y < H; ++y) {
				for (int x = 0; x < W; ++x) {
					// Span header
					buf.push_back(0);         // number_4byte_chunks = 0 (last span)
					buf.push_back(GROUND_Z);  // top_color_start
					buf.push_back(GROUND_Z);  // top_color_end (inclusive)
					buf.push_back(0);         // air_start
					// Color entry (matches WriteColor byte order: R, G, B, A)
					buf.push_back(COLOR_R);
					buf.push_back(COLOR_G);
					buf.push_back(COLOR_B);
					buf.push_back(COLOR_A);
				}
			}

			return buf;
		}

	} // namespace tests
} // namespace spades
