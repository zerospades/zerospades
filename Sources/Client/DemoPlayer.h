/*
 Copyright (c) 2026 Francois ND
 based on code of OpenSpades (c) yvt 2013.

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
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <Core/Stopwatch.h>

namespace spades {
	namespace client {

		/**
		 * Plays back demo files recorded in aos_replay compatible format.
		 *
		 * File format:
		 * - Header: 2 bytes
		 *   - Byte 0: File version (1)
		 *   - Byte 1: Protocol version (3 for 0.75, 4 for 0.76)
		 * - Packets: variable length entries
		 *   - 4 bytes: timestamp (float, seconds since recording start)
		 *   - 2 bytes: packet length (uint16)
		 *   - N bytes: packet data
		 */
		class DemoPlayer {
		public:
			static constexpr uint8_t FILE_VERSION = 1;

			using PacketHandler = std::function<void(const std::vector<char>&)>;
			using TimedPacketHandler = std::function<void(const std::vector<char>&, float dt)>;

			DemoPlayer();
			~DemoPlayer();

			/**
			 * Opens a demo file for playback.
			 * @param filename Path to the demo file
			 * @return true if the file was opened successfully
			 */
			bool Open(const std::string& filename);

			/**
			 * Closes the current demo file.
			 */
			void Close();

			/**
			 * Updates playback and dispatches packets that should be played.
			 * @param dt Delta time since last update
			 * @param handler Callback for each packet to be played
			 * @return Number of packets dispatched
			 */
			int Update(float dt, const PacketHandler& handler);

			/**
			 * Seeks to a specific time in the demo.
			 * @param time Time in seconds from the start
			 */
			void Seek(float time);

			/**
			 * Fast-forward by a number of seconds.
			 * @param seconds Seconds to skip forward
			 */
			void FastForward(float seconds);

			/**
			 * Pauses playback.
			 */
			void Pause();

			/**
			 * Resumes playback.
			 */
			void Resume();

			/**
			 * Toggles pause state.
			 */
			void TogglePause();

			/**
			 * Sets the playback speed multiplier.
			 * @param speed Speed multiplier (1.0 = normal, 2.0 = double speed)
			 */
			void SetSpeed(float speed);

			/**
			 * @return true if a demo is currently loaded
			 */
			bool IsOpen() const { return isOpen; }

			/**
			 * @return true if playback is paused
			 */
			bool IsPaused() const { return paused; }

			/**
			 * @return true if playback has finished
			 */
			bool IsFinished() const { return finished; }

			/**
			 * @return Current playback time in seconds
			 */
			float GetTime() const { return playbackTime; }

			/**
			 * @return Total demo duration in seconds (approximate)
			 */
			float GetDuration() const { return duration; }

			/**
			 * @return Protocol version of the demo (3 for 0.75, 4 for 0.76)
			 */
			int GetProtocolVersion() const { return protocolVersion; }

			/**
			 * @return Current playback speed multiplier
			 */
			float GetSpeed() const { return speed; }

			/**
			 * @return The filename of the loaded demo
			 */
			const std::string& GetFilename() const { return filename; }

			/**
			 * @return Current packet index
			 */
			size_t GetCurrentPacketIndex() const { return currentPacketIndex; }

			/**
			 * Timestamp of the last packet that is part of the initial-state stream
			 * (MapStart / MapChunk / StateData / ExistingPlayer). These packets are
			 * written by the recorder immediately after the stopwatch starts, so
			 * they always carry tiny but strictly positive timestamps; seek logic
			 * uses this value to ensure they are always re-applied when rebuilding
			 * the world.
			 *
			 * @return Bootstrap-end timestamp in seconds, or 0 if there is no
			 *         identifiable bootstrap prefix.
			 */
			float GetBootstrapEndTime() const { return bootstrapEndTime; }

			/**
			 * Calls handler for every packet whose timestamp is <= targetTime, in order.
			 * The handler also receives the time delta from the previous packet (or 0
			 * for the first), so callers can age the world in step with playback time
			 * (e.g. to fire grenade fuses silently during a forward seek).
			 * Does NOT modify playbackTime, currentPacketIndex, or paused state; safe to
			 * call during a backward-seek reset without disrupting normal playback state.
			 * @param targetTime Upper bound (inclusive) on packet timestamps to replay
			 * @param handler    Callback invoked for each matching packet, with dt
			 */
			void ReplayUpTo(float targetTime, const TimedPacketHandler& handler) const;

			/**
			 * Resets playback to the beginning.
			 */
			void Reset();

		private:
			struct DemoPacket {
				float timestamp;
				std::vector<char> data;
			};

			std::ifstream file;
			std::string filename;
			bool isOpen;
			bool paused;
			bool finished;
			int protocolVersion;
			float playbackTime;
			float duration;
			float speed;
			float bootstrapEndTime;

			// Pre-loaded packets for seeking
			std::vector<DemoPacket> packets;
			size_t currentPacketIndex;

			/**
			 * Reads the file header and validates it.
			 * @return true if the header is valid
			 */
			bool ReadHeader();

			/**
			 * Pre-loads all packets from the file for seeking support.
			 * @return true if successful
			 */
			bool PreloadPackets();
		};

	} // namespace client
} // namespace spades
