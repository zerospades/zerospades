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
#include <Client/Player.h>

namespace spades {
	namespace tests {

		/**
		 * Serialize one physics tick from a live Player into a JSON object.
		 *
		 * Fields captured:
		 *   tick (int), player_id (int, always 0 for single-player fixtures),
		 *   position {x,y,z}, velocity {x,y,z}, orientation {x,y,z},
		 *   airborne (bool), wade (bool, raw BoxClipMove bool via IsWading()).
		 *
		 * Float fields are cast to double before insertion for lossless round-trip
		 * precision (avoids nlohmann float truncation). Tolerance comparison is
		 * a separate concern handled by ToleranceMatchers.h / ExpectSnapshotMatches.
		 *
		 * orientation is the non-interpolated front vector (GetFront(false)).
		 * wade is the raw bool from BoxClipMove:1105 (position.z > 61.0F),
		 * exposed by IsWading(); distinct from GetWade() which uses origin offset.
		 */
		nlohmann::json SnapshotPlayerTick(const client::Player& p, int tick);

	} // namespace tests
} // namespace spades
