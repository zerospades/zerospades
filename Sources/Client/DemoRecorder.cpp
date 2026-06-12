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
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "DemoRecorder.h"
#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>

namespace spades {
	namespace client {

		DemoRecorder::DemoRecorder() : recording(false), packetCount(0) {}

		DemoRecorder::~DemoRecorder() {
			if (recording)
				StopRecording();
		}

		bool DemoRecorder::StartRecording(const std::string& fname, int protocolVersion) {
			SPADES_MARK_FUNCTION();

			if (recording) {
				SPLog("Already recording, stopping previous recording");
				StopRecording();
			}

			filename = fname; // relative

			try {
				stream = FileManager::OpenForWriting(filename.c_str());
			} catch (const std::exception& ex) {
				SPLog("Failed to open demo file for writing: %s (%s)", filename.c_str(), ex.what());
				return false;
			}

			// Write header: file version and protocol version
			uint8_t header[2];
			header[0] = FILE_VERSION;
			header[1] = static_cast<uint8_t>(protocolVersion);
			try {
				stream->Write(header, 2);
			} catch (const std::exception& ex) {
				SPLog("Failed to write demo header: %s", ex.what());
				stream.reset();
				return false;
			}

			packetCount = 0;
			stopwatch.Reset();
			recording = true;

			SPLog("Started demo recording: %s (protocol version %d)", filename.c_str(), protocolVersion);
			return true;
		}

		void DemoRecorder::StopRecording() {
			SPADES_MARK_FUNCTION();

			if (!recording)
				return;

			try {
				stream->Flush();
			} catch (...) {}

			float elapsed = GetRecordingTime();
			stream.reset();
			recording = false;

			SPLog("Stopped demo recording: %s (%llu packets, %.1f seconds)",
				  filename.c_str(), (unsigned long long)packetCount, elapsed);
		}

		void DemoRecorder::RecordPacket(const char* data, size_t length) {
			SPADES_MARK_FUNCTION_DEBUG();

			if (!recording || length == 0 || length > 65535)
				return;

			try {
				float timestamp = static_cast<float>(stopwatch.GetTime());
				stream->Write(&timestamp, sizeof(float));

				uint16_t len = static_cast<uint16_t>(length);
				stream->Write(&len, sizeof(uint16_t));

				stream->Write(data, length);
				packetCount++;
			} catch (const std::exception& ex) {
				SPLog("Failed to write packet to demo file: %s", ex.what());
			}
		}

		float DemoRecorder::GetRecordingTime() const {
			if (!recording)
				return 0.0F;
			return static_cast<float>(stopwatch.GetTime());
		}

		std::string DemoRecorder::SanitizeComponent(const std::string& s) {
			std::string out;
			out.reserve(s.size());
			for (unsigned char c : s) {
				if (std::isalnum(c))
					out += static_cast<char>(std::tolower(c));
				else if (!out.empty() && out.back() != '_')
					out += '_';
			}
			while (!out.empty() && out.back() == '_')
				out.pop_back();
			return out;
		}

		std::string DemoRecorder::GenerateFilename(const std::string& context) {
			auto now = std::chrono::system_clock::now();
			auto time = std::chrono::system_clock::to_time_t(now);
			auto tm = std::localtime(&time);

			std::ostringstream oss;
			oss << "Demos/" << std::put_time(tm, "%Y-%m-%d-%H-%M");
			if (!context.empty())
				oss << "-" << context;
			oss << ".dem";

			return oss.str();
		}

		static std::vector<std::string> ScanDemosDir() {
			return FileManager::EnumFiles("Demos");
		}

		std::vector<std::string> DemoRecorder::ListRecordings() {
			auto files = ScanDemosDir();
			std::vector<std::string> result;
			for (auto& f : files) {
				if (f.size() > 4 && f.substr(f.size() - 4) == ".dem")
					result.push_back("Demos/" + f);
			}
			std::sort(result.begin(), result.end());
			return result;
		}

		void DemoRecorder::PruneOldRecordings(size_t maxCount) {
			std::vector<std::string> files = ListRecordings();
			if (files.size() <= maxCount)
				return;
			size_t toRemove = files.size() - maxCount;
			for (size_t i = 0; i < toRemove; i++) {
				if (FileManager::RemoveFile(files[i].c_str())) {
					SPLog("Pruned old demo recording: %s", files[i].c_str());
				} else {
					SPLog("Failed to prune demo recording: %s", files[i].c_str());
				}
			}
		}
	} // namespace client
} // namespace spades
