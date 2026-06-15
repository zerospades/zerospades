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

#include <nlohmann/json.hpp>
#include <Client/Grenade.h>

namespace spades {
	namespace tests {

		/**
		 * Serialize one trajectory tick from a live Grenade into a JSON object.
		 *
		 * Fields captured (matching the GrenadeSnapshot $def in
		 * fixtures/fixture_schema.json, required ["position","velocity","fuse_s"]):
		 *   position {x,y,z}, velocity {x,y,z}, fuse_s (float).
		 *
		 * Float fields are cast to double before insertion for lossless round-trip
		 * precision (avoids nlohmann float truncation). Tolerance comparison is a
		 * separate concern handled by ToleranceMatchers.h / ExpectSnapshotMatches.
		 *
		 * There is intentionally NO orientation field: the quaternion rotation in
		 * Grenade::Update (Grenade.cpp:61-69) is NOT part of grenade physics
		 * (Grenade.h:36-38 FIXME) and is deliberately not characterized
		 * (07-RESEARCH Pattern 4). Grenade getters are const (Grenade.h:53-56) — no cast.
		 */
		nlohmann::json SnapshotGrenadeTick(const client::Grenade& g);

	} // namespace tests
} // namespace spades
