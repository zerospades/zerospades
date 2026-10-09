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

#include "DaytimeWeather.h"

#include <cstdint>

#include <Core/Debug.h>

#include "NetProtocol.h"

namespace spades {
	namespace client {
		namespace {
			/**
			 * The Times a Sky is sent with. Version 1 reads any but `0` as the day; these
			 * are midnight and noon, which a version with a passing time will read as
			 * minutes since midnight.
			 */
			constexpr int kNightTime = 0;
			constexpr int kDayTime = 720;

			void WriteUShort(std::vector<char>& data, int value) {
				data.push_back(static_cast<char>(value & 0xff));
				data.push_back(static_cast<char>((value >> 8) & 0xff));
			}
		} // namespace

		void ApplyDaytimeWeatherPacket(NetPacketReader& r, stmp::optional<TimeOfDay>& timeOfDay) {
			SPADES_MARK_FUNCTION();

			if (!HasSubPacketBytes(r, 1, "Daytime and Weather"))
				return;

			switch (r.ReadByte()) { // sub packet id
				case DaytimeWeatherSubSky: {
					if (!HasSubPacketBytes(r, kDaytimeWeatherSkyBytes, "Daytime and Weather"))
						break;

					const int time = r.ReadShort();
					r.ReadShort(); // reserved
					r.ReadShort(); // weather, unimplemented in version 1

					timeOfDay = time == kNightTime ? TimeOfDay::Night : TimeOfDay::Day;
				} break;
				default:
					// An unknown sub packet belongs to a newer version of the extension.
					SPLog("Ignoring an unknown Daytime and Weather sub packet");
					break;
			}
		}

		std::vector<char> EncodeDaytimeWeatherSky(TimeOfDay timeOfDay) {
			std::vector<char> data{static_cast<char>(PacketTypeDaytimeWeather),
			                       static_cast<char>(DaytimeWeatherSubSky)};
			WriteUShort(data, timeOfDay == TimeOfDay::Night ? kNightTime : kDayTime);
			WriteUShort(data, 0); // reserved
			WriteUShort(data, 0); // weather
			return data;
		}

		float GetSunlight(TimeOfDay timeOfDay) {
			return timeOfDay == TimeOfDay::Night ? 0.0F : 1.0F;
		}

		float GetDaylight(TimeOfDay timeOfDay) {
			return timeOfDay == TimeOfDay::Night ? 0.0F : 1.0F;
		}
	} // namespace client
} // namespace spades
