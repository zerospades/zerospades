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

#include "Flashlight.h"

#include <string>

#include <Core/Debug.h>

#include "Client.h"
#include "NetProtocol.h"
#include "Player.h"
#include "World.h"

namespace spades {
	namespace client {
		namespace {
			std::vector<char> StartPacket(FlashlightSubPacketType sub) {
				return {static_cast<char>(PacketTypeFlashlight), static_cast<char>(sub)};
			}
		} // namespace

		const FlashlightBeam& FlashlightBeams::Resolve(int playerId) const {
			static const FlashlightBeam builtIn;

			auto it = players.find(playerId);
			if (it != players.end())
				return it->second;
			return serverDefault ? *serverDefault : builtIn;
		}

		void ApplyFlashlightPacket(NetPacketReader& r, Client& client, FlashlightBeams& beams,
		                           bool silent) {
			SPADES_MARK_FUNCTION();

			if (!HasSubPacketBytes(r, 1, "Flashlight"))
				return;

			// Every id byte has a slot, so a raw id indexes the world safely.
			World* world = client.GetWorld();

			switch (r.ReadByte()) { // sub packet id
				case FlashlightSubLight: {
					if (!HasSubPacketBytes(r, kFlashlightLightBytes, "Flashlight"))
						break;

					std::uint8_t playerId = r.ReadByte();
					bool on = r.ReadByte() != 0;

					if (!world)
						break;

					stmp::optional<Player&> player = world->GetPlayer(playerId);
					if (!player || player->IsFlashlightOn() == on)
						break;

					player->SetFlashlightOn(on);
					if (!silent)
						client.PlayerSwitchedFlashlight(*player);
				} break;
				case FlashlightSubLightState: {
					const std::string states = r.ReadRemainingData();

					if (!world)
						break;

					for (std::size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
						stmp::optional<Player&> player =
						  world->GetPlayer(static_cast<unsigned int>(i));
						if (!player)
							continue;

						const std::size_t byte = i / 8;
						const bool on =
						  byte < states.size() &&
						  ((static_cast<std::uint8_t>(states[byte]) >> (i % 8)) & 1) != 0;
						player->SetFlashlightOn(on);
					}
				} break;
				case FlashlightSubLightConfig: {
					if (!HasSubPacketBytes(r, kFlashlightConfigBytes, "Flashlight"))
						break;

					int playerId = r.ReadByte();

					FlashlightBeam beam;
					beam.reach = r.ReadByte();
					beam.cone = r.ReadByte();
					beam.red = r.ReadByte();
					beam.green = r.ReadByte();
					beam.blue = r.ReadByte();

					if (playerId == kServerPlayerId)
						beams.SetDefault(beam);
					else
						beams.Set(playerId, beam);
				} break;
				default:
					// An unknown sub packet belongs to a newer version of the extension.
					SPLog("Ignoring an unknown Flashlight sub packet");
					break;
			}
		}

		std::vector<char> EncodeFlashlightLight(int playerId, bool on) {
			std::vector<char> data = StartPacket(FlashlightSubLight);
			data.push_back(static_cast<char>(playerId));
			data.push_back(on ? 1 : 0);
			return data;
		}

		std::vector<char> EncodeFlashlightLightState(World& world) {
			std::vector<char> data = StartPacket(FlashlightSubLightState);
			const std::size_t header = data.size();
			data.resize(header + (world.GetNumPlayerSlots() + 7) / 8, 0);

			for (std::size_t i = 0; i < world.GetNumPlayerSlots(); i++) {
				stmp::optional<Player&> player = world.GetPlayer(static_cast<unsigned int>(i));
				if (player && player->IsFlashlightOn())
					data[header + i / 8] |= static_cast<char>(1 << (i % 8));
			}
			return data;
		}

		std::vector<char> EncodeFlashlightLightConfig(int playerId, const FlashlightBeam& beam) {
			std::vector<char> data = StartPacket(FlashlightSubLightConfig);
			for (std::uint8_t field : {static_cast<std::uint8_t>(playerId), beam.reach, beam.cone,
			                           beam.red, beam.green, beam.blue})
				data.push_back(static_cast<char>(field));
			return data;
		}
	} // namespace client
} // namespace spades
