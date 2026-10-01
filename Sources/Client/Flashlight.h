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

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <Core/Math.h>
#include <Core/TMPUtils.h>

namespace spades {
	namespace client {
		class Client;
		class NetPacketReader;
		class World;

		/**
		 * One flashlight beam, kept in the units of the *Flashlight* extension's Light
		 * Config so that it can be sent on exactly as it arrived. The defaults are the
		 * beam of every flashlight the server has not configured.
		 */
		struct FlashlightBeam {
			std::uint8_t reach = 60; // blocks at which the light reaches zero
			std::uint8_t cone = 90;  // full angle, in degrees
			std::uint8_t red = 255;  // linear, `255` is `1.0`
			std::uint8_t green = 179;
			std::uint8_t blue = 128;

			float GetReach() const { return reach; }
			float GetConeAngle() const { return DEG2RAD(cone); }
			Vector3 GetColor() const { return MakeVector3(red, green, blue) / 255.0F; }

			/** Whether the beam lights anything at all. */
			bool Emits() const { return reach > 0 && cone > 0 && (red | green | blue) != 0; }
		};

		/** The beams the server configured: its default for every player without one of
		 * their own, and each player's. They belong to the connection, not to a world or
		 * a player object, so they last through death, respawn and map changes. */
		class FlashlightBeams {
			stmp::optional<FlashlightBeam> serverDefault;
			std::unordered_map<int, FlashlightBeam> players;

		public:
			void SetDefault(const FlashlightBeam& beam) { serverDefault = beam; }
			void Set(int playerId, const FlashlightBeam& beam) { players[playerId] = beam; }
			void Forget(int playerId) { players.erase(playerId); }
			void Clear() {
				serverDefault.reset();
				players.clear();
			}

			/** The beam a player's flashlight has. */
			const FlashlightBeam& Resolve(int playerId) const;

			const stmp::optional<FlashlightBeam>& GetDefault() const { return serverDefault; }
			const std::unordered_map<int, FlashlightBeam>& GetPlayers() const {
				return players;
			}
		};

		/**
		 * Applies one *Flashlight* extension packet, received or replayed from a demo.
		 *
		 * Light and Light State switch players of the current world and are dropped
		 * without one: the Map Start that creates the next world turns every light off
		 * anyway. A truncated or unknown sub packet is ignored.
		 *
		 * `silent` skips the switch sound, for a demo seek.
		 */
		void ApplyFlashlightPacket(NetPacketReader&, Client&, FlashlightBeams&, bool silent);

		/** Whole Flashlight packets, packet id included, laid out as
		 * `ApplyFlashlightPacket` reads them. */
		std::vector<char> EncodeFlashlightLight(int playerId, bool on);
		std::vector<char> EncodeFlashlightLightState(World&);
		std::vector<char> EncodeFlashlightLightConfig(int playerId, const FlashlightBeam&);
	} // namespace client
} // namespace spades
