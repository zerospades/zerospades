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

#include <Core/TMPUtils.h>

namespace spades {
	namespace client {
		class NetPacketReader;

		/** The time of day the *Daytime and Weather* extension's Sky sets. */
		enum class TimeOfDay { Day, Night };

		/**
		 * Applies one *Daytime and Weather* packet, received or replayed from a demo, to
		 * `timeOfDay`. A truncated or unknown sub packet is ignored.
		 */
		void ApplyDaytimeWeatherPacket(NetPacketReader&, stmp::optional<TimeOfDay>& timeOfDay);

		/** A whole Sky packet, packet id included, laid out as
		 * `ApplyDaytimeWeatherPacket` reads it. */
		std::vector<char> EncodeDaytimeWeatherSky(TimeOfDay);

		/** The factor the sun's light is drawn with: none at night, when the sun casts
		 * no light and no shadow. */
		float GetSunlight(TimeOfDay);

		/** The factor the rest of the world's lighting, the fog and the sky are drawn
		 * with. `1` is the day, and how the world looks without the extension; at night
		 * there is none, and only dynamic lights light the world. */
		float GetDaylight(TimeOfDay);
	} // namespace client
} // namespace spades
