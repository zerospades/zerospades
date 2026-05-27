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
#include <memory>
#include <string>
#include <vector>

#include <Core/Stopwatch.h>

namespace spades {
	namespace client {

		/**
		 * Records gameplay to a demo file compatible with aos_replay format.
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
		class DemoRecorder {
		public:
			static constexpr uint8_t FILE_VERSION = 1;

			DemoRecorder();
			~DemoRecorder();

			/**
			 * Starts recording to a file.
			 * @param filename Path to the demo file
			 * @param protocolVersion Protocol version (3 for 0.75, 4 for 0.76)
			 * @return true if recording started successfully
			 */
			bool StartRecording(const std::string& filename, int protocolVersion);

			/**
			 * Stops recording and closes the file.
			 */
			void StopRecording();

			/**
			 * Records a packet to the demo file.
			 * @param data Packet data
			 * @param length Packet length
			 */
			void RecordPacket(const char* data, size_t length);

			/**
			 * @return true if currently recording
			 */
			bool IsRecording() const { return recording; }

			/**
			 * @return Current recording duration in seconds
			 */
			float GetRecordingTime() const;

			/**
			 * @return Number of packets recorded
			 */
			uint64_t GetPacketCount() const { return packetCount; }

			/**
			 * @return The filename being recorded to
			 */
			const std::string& GetFilename() const { return filename; }

			/**
			 * Sanitizes a single filename component (map name, server address, etc.).
			 * Lowercases alphanumeric characters; collapses runs of other characters
			 * to a single '_'. The '-' separator between components is never emitted.
			 */
			static std::string SanitizeComponent(const std::string& s);

			/**
			 * Generates a unique filename for a new demo in the Demos/ directory.
			 * @param context Pre-sanitized context string appended after the timestamp.
			 *                Individual fields must be joined with '-' by the caller.
			 *                Format: Demos/YYYY-MM-DD-HH-MM[-context].dem
			 */
			static std::string GenerateFilename(const std::string& context = "");

			/**
			 * Removes the oldest .dem files in Demos/ so at most maxCount remain.
			 * Files are ordered by name (which matches recording order given the timestamp prefix).
			 */
			static void PruneOldRecordings(size_t maxCount);

			/**
			 * Returns a sorted list of .dem file paths found in Demos/.
			 */
			static std::vector<std::string> ListRecordings();

			/**
			 * Sets the base directory under which the Demos/ subfolder is created.
			 * Must be called before any recording or listing takes place.
			 * Defaults to the current working directory if never called.
			 */
			static void SetBaseDirectory(const std::string& dir);

			/** Returns the absolute path to the Demos directory. */
			static std::string GetDemosDirectory();

		private:
			std::ofstream file;
			Stopwatch stopwatch;
			bool recording;
			std::string filename;
			uint64_t packetCount;
		};

	} // namespace client
} // namespace spades
