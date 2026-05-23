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

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Client/GameProperties.h>
#include <Client/World.h>

#include "RecordingWorldListener.h"

namespace spades {
	namespace tests {

		/** Fixed timestep used by production Client_Update.cpp (1.0F / 60.0F). */
		static constexpr float FIXED_DT = 1.0F / 60.0F;

		/**
		 * Thin facade that owns World + GameMap + RecordingWorldListener lifetime.
		 *
		 * Construction:
		 *   1. Seeds the calling thread's RNG via SeedLocalRNG (BEFORE GameMap::Load).
		 *   2. Constructs a GameProperties (v075).
		 *   3. Constructs a World with those properties.
		 *   4. Loads the map from the caller-supplied VOXLAP5 byte buffer via MemoryStream.
		 *   5. Installs a RecordingWorldListener.
		 *
		 * Advance(n) calls World::Advance(FIXED_DT) exactly n times.
		 *
		 * No SDL, OpenGL, ENet, or AngelScript is touched.
		 */
		class HeadlessWorld {
		public:
			/**
			 * @param rngSeed    Seed passed to SeedLocalRNG before map loading.
			 * @param vxlBytes   VOXLAP5 buffer — typically from MakeFlatMapBytes().
			 */
			HeadlessWorld(std::uint64_t rngSeed, const std::vector<uint8_t>& vxlBytes);
			~HeadlessWorld();

			/** Advance the world by `ticks` fixed timesteps of FIXED_DT seconds each. */
			void Advance(int ticks = 1);

			client::World& GetWorld() { return *world_; }
			RecordingWorldListener& GetListener() { return *listener_; }

		private:
			std::shared_ptr<client::GameProperties> props_;
			std::unique_ptr<client::World> world_;
			std::unique_ptr<RecordingWorldListener> listener_;
		};

	} // namespace tests
} // namespace spades
