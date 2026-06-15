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

#include "HeadlessWorld.h"

#include <Client/GameMap.h>
#include <Client/GameProperties.h>
#include <Client/World.h>
#include <Core/Exception.h>
#include <Core/Math.h>
#include <Core/MemoryStream.h>
#include <Core/RefCountedObject.h>
#include <Core/ServerAddress.h>

#include "RecordingWorldListener.h"

namespace spades {
	namespace tests {

		HeadlessWorld::HeadlessWorld(std::uint64_t rngSeed, const std::vector<uint8_t>& vxlBytes) {
			// 1. Seed RNG BEFORE any GameMap construction to ensure determinism.
			//    GameMap::GetDirtColor() calls SampleRandom() if the default ctor is used;
			//    we use Load() instead, but seeding first is the safe invariant.
			SeedLocalRNG(rngSeed, rngSeed ^ 0x9e3779b97f4a7c15ULL);

			// 2. Construct GameProperties (v075 — matches typical test server baseline).
			props_ = std::make_shared<client::GameProperties>(ProtocolVersion::v075);

			// 3. Construct World — takes only GameProperties; no renderer or audio required.
			world_ = std::make_unique<client::World>(props_);

			// 4. Attach listener before loading map (safe order).
			listener_ = std::make_unique<RecordingWorldListener>();
			world_->SetListener(listener_.get());

			// 5. Load map via MemoryStream — bypasses GameMapLoader background thread.
			MemoryStream stream(reinterpret_cast<const char*>(vxlBytes.data()), vxlBytes.size());
			client::GameMap* rawMap = client::GameMap::Load(&stream);
			if (!rawMap) {
				SPRaise("HeadlessWorld: GameMap::Load returned null");
			}
			Handle<client::GameMap> map{rawMap};
			world_->SetMap(map);
		}

		HeadlessWorld::~HeadlessWorld() = default;

		void HeadlessWorld::Advance(int ticks) {
			for (int i = 0; i < ticks; ++i) {
				world_->Advance(FIXED_DT);
			}
		}

	} // namespace tests
} // namespace spades
