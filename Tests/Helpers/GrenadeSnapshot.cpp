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

#include "GrenadeSnapshot.h"

namespace spades {
	namespace tests {

		nlohmann::json SnapshotGrenadeTick(const client::Grenade& g) {
			// Grenade physics getters are const (Grenade.h:53-56) — no const_cast needed.
			auto pos = g.GetPosition(); // Grenade.h:53
			auto vel = g.GetVelocity(); // Grenade.h:54

			// Cast float fields to double for lossless JSON serialization.
			// Field names MUST be position/velocity/fuse_s to match the GrenadeSnapshot
			// $def (fixtures/fixture_schema.json). NO orientation (see header doc-comment).
			return nlohmann::json{
				{"position", {{"x", static_cast<double>(pos.x)},
				              {"y", static_cast<double>(pos.y)},
				              {"z", static_cast<double>(pos.z)}}},
				{"velocity", {{"x", static_cast<double>(vel.x)},
				              {"y", static_cast<double>(vel.y)},
				              {"z", static_cast<double>(vel.z)}}},
				{"fuse_s", static_cast<double>(g.GetFuse())}, // Grenade.h:56
			};
		}

	} // namespace tests
} // namespace spades
