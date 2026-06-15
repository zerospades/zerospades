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

#include "PlayerPhysicsSnapshot.h"

namespace spades {
	namespace tests {

		nlohmann::json SnapshotPlayerTick(const client::Player& p, int tick) {
			// Non-const getters: cast away constness to call them.
			// Player physics getters are not const in the production API; the
			// snapshot function takes const& for read-only intent but must call
			// through a mutable pointer for the existing non-const getters.
			client::Player& pm = const_cast<client::Player&>(p);

			auto pos = pm.GetPosition();        // Player.h:219
			auto vel = pm.GetVelocity();        // Player.h:226
			auto ori = pm.GetFront(false);      // Player.h:220, non-interpolated (Pitfall 5)
			bool airborne = pm.IsAirborne();    // Player.h:234
			bool wade = p.IsWading();           // Player.h: raw BoxClipMove bool (const)

			// Cast float fields to double for lossless JSON serialization (per RESEARCH Q6/A2).
			return nlohmann::json{
				{"tick",        tick},
				{"player_id",   0},
				{"position",    {{"x", static_cast<double>(pos.x)},
				                 {"y", static_cast<double>(pos.y)},
				                 {"z", static_cast<double>(pos.z)}}},
				{"velocity",    {{"x", static_cast<double>(vel.x)},
				                 {"y", static_cast<double>(vel.y)},
				                 {"z", static_cast<double>(vel.z)}}},
				{"orientation", {{"x", static_cast<double>(ori.x)},
				                 {"y", static_cast<double>(ori.y)},
				                 {"z", static_cast<double>(ori.z)}}},
				{"airborne",    airborne},
				{"wade",        wade},
			};
		}

	} // namespace tests
} // namespace spades
