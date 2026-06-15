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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include <algorithm>

#include "DemoPlayer.h"
#include "ProtocolCodec.h"

#include <Core/Debug.h>
#include <Core/FileManager.h>

namespace spades {
	namespace client {

		DemoPlayer::DemoPlayer()
			: isOpen(false),
			  paused(false),
			  finished(false),
			  protocolVersion(0),
			  playbackTime(0.0F),
			  duration(0.0F),
			  speed(1.0F),
			  bootstrapEndTime(0.0F),
			  currentPacketIndex(0) {}

		DemoPlayer::~DemoPlayer() {
			Close();
		}

		bool DemoPlayer::Open(const std::string& fname) {
			SPADES_MARK_FUNCTION();

			Close();

			try {
				stream = FileManager::OpenForReading(fname.c_str());
			} catch (const std::exception& ex) {
				SPLog("Failed to open demo file: %s (%s)", fname.c_str(), ex.what());
				return false;
			}

			filename = fname;

			if (!ReadHeader()) {
				stream.reset();
				return false;
			}

			if (!PreloadPackets()) {
				stream.reset();
				return false;
			}

			isOpen = true;
			paused = false;
			finished = false;
			playbackTime = 0.0F;
			currentPacketIndex = 0;

			SPLog("Opened demo file: %s (protocol %d, %.1f seconds, %zu packets)",
				  filename.c_str(), protocolVersion, duration, packets.size());

			return true;
		}

		void DemoPlayer::Close() {
			SPADES_MARK_FUNCTION();

			stream.reset();

			isOpen = false;
			paused = false;
			finished = false;
			playbackTime = 0.0F;
			duration = 0.0F;
			bootstrapEndTime = 0.0F;
			currentPacketIndex = 0;
			packets.clear();
			filename.clear();
		}

		bool DemoPlayer::ReadHeader() {
			SPADES_MARK_FUNCTION();

			uint8_t header[2];
			if (stream->Read(header, 2) != 2) {
				SPLog("Failed to read demo header");
				return false;
			}

			if (header[0] != FILE_VERSION) {
				SPLog("Unsupported demo file version: %d (expected %d)", header[0], FILE_VERSION);
				return false;
			}

			protocolVersion = header[1];
			if (protocolVersion != 3 && protocolVersion != 4) {
				SPLog("Unsupported protocol version: %d", protocolVersion);
				return false;
			}

			return true;
		}

		bool DemoPlayer::PreloadPackets() {
			SPADES_MARK_FUNCTION();

			packets.clear();
			duration = 0.0F;

			while (true) {
				float timestamp;
				if (stream->Read(&timestamp, sizeof(float)) != sizeof(float))
					break;

				uint16_t length;
				if (stream->Read(&length, sizeof(uint16_t)) != sizeof(uint16_t))
					break;

				if (length == 0 || length > 65535) {
					SPLog("Invalid packet length: %u", length);
					break;
				}

				DemoPacket packet;
				packet.timestamp = timestamp;
				packet.data.resize(length);
				if (stream->Read(packet.data.data(), length) != length)
					break;

				packets.push_back(std::move(packet));
				duration = timestamp;
			}

			if (packets.empty()) {
				SPLog("No packets found in demo file");
				return false;
			}

			// Identify the prefix that constitutes the initial-state stream so
			// backward seeks can clamp to it: the recorder writes MapStart,
			// MapChunk, StateData and ExistingPlayer back-to-back at the very
			// start of a demo, each timestamped with the running stopwatch (so
			// strictly > 0). Without this clamp, Seek(0) would land before the
			// bootstrap and leave the world in an empty state on rebuild.
			bootstrapEndTime = 0.0F;
			for (const auto& pkt : packets) {
				if (pkt.data.empty())
					break;
				uint8_t type = static_cast<uint8_t>(pkt.data[0]);
				if (type != PacketTypeMapStart
				 && type != PacketTypeMapChunk
				 && type != PacketTypeStateData
				 && type != PacketTypeExistingPlayer)
					break;
				bootstrapEndTime = pkt.timestamp;
			}

			// All data is preloaded; release the file handle.
			stream.reset();

			return true;
		}

		int DemoPlayer::Update(float dt, const PacketHandler& handler) {
			if (!isOpen || finished || paused)
				return 0;

			playbackTime += dt * speed;
			if (playbackTime >= duration)
				playbackTime = duration;

			int dispatched = 0;
			while (currentPacketIndex < packets.size()) {
				const auto& packet = packets[currentPacketIndex];
				if (packet.timestamp > playbackTime)
					break;

				handler(packet.data);
				currentPacketIndex++;
				dispatched++;
			}

			if (currentPacketIndex >= packets.size())
				finished = true;

			return dispatched;
		}

		void DemoPlayer::Seek(float time) {
			if (!isOpen)
				return;

			// Refuse to land before the bootstrap region. Anything earlier
			// would correspond to an empty world (no map metadata, no game
			// mode, no players), which is never a valid playback state.
			time = std::max(time, bootstrapEndTime);

			playbackTime = std::max(0.0F, std::min(time, duration));
			finished = (currentPacketIndex >= packets.size());

			// Find the first packet with timestamp > playbackTime (O(log n)).
			// Update() will start dispatching from this index, so no packet
			// at or before the seek point gets re-dispatched.
			auto it = std::upper_bound(packets.begin(), packets.end(), playbackTime,
				[](float t, const DemoPacket& p) { return t < p.timestamp; });
			currentPacketIndex = static_cast<size_t>(std::distance(packets.begin(), it));
		}

		void DemoPlayer::FastForward(float seconds) { Seek(playbackTime + seconds); }
		void DemoPlayer::Pause() { paused = true; }
		void DemoPlayer::Resume() { paused = false; }
		void DemoPlayer::TogglePause() { paused = !paused; }
		void DemoPlayer::SetSpeed(float s) { speed = std::max(0.1F, std::min(s, 10.0F)); }

		void DemoPlayer::ReplayUpTo(float targetTime, const TimedPacketHandler& handler) const {
			auto end = std::upper_bound(packets.begin(), packets.end(), targetTime,
				[](float t, const DemoPacket& p) { return t < p.timestamp; });
			float prev = 0.0F;
			for (auto it = packets.begin(); it != end; ++it) {
				float dt = std::max(0.0F, it->timestamp - prev);
				handler(it->data, dt);
				prev = it->timestamp;
			}
		}

		void DemoPlayer::Reset() {
			if (!isOpen)
				return;
			playbackTime = 0.0F;
			currentPacketIndex = 0;
			finished = false;
			paused = false;
		}

	} // namespace client
} // namespace spades
