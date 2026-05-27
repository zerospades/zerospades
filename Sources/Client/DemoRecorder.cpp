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

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(path) _mkdir(path)
#else
#include <dirent.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

#include "DemoRecorder.h"
#include <Core/Debug.h>

namespace spades {
	namespace client {

		static std::string s_baseDir;

		void DemoRecorder::SetBaseDirectory(const std::string& dir) {
			s_baseDir = dir;
		}

		std::string DemoRecorder::GetDemosDirectory() {
			return s_baseDir.empty() ? "Demos" : s_baseDir + "/Demos";
		}

		DemoRecorder::DemoRecorder()
		    : recording(false), packetCount(0) {}

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

			filename = fname;

			file.open(filename, std::ios::binary | std::ios::out);
			if (!file.is_open()) {
				SPLog("Failed to open demo file for writing: %s", filename.c_str());
				return false;
			}

			// Write header: file version and protocol version
			uint8_t header[2];
			header[0] = FILE_VERSION;
			header[1] = static_cast<uint8_t>(protocolVersion);
			file.write(reinterpret_cast<const char*>(header), 2);

			if (!file.good()) {
				SPLog("Failed to write demo header");
				file.close();
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

			file.flush();
			uint64_t fileSize = static_cast<uint64_t>(file.tellp());
			float elapsed = GetRecordingTime();
			file.close();
			recording = false;

			SPLog("Stopped demo recording: %s (%llu packets, %llu bytes, %.1f seconds)",
			      filename.c_str(), (unsigned long long)packetCount,
			      (unsigned long long)fileSize, elapsed);
		}

		void DemoRecorder::RecordPacket(const char* data, size_t length) {
			SPADES_MARK_FUNCTION_DEBUG();

			if (!recording || length == 0 || length > 65535)
				return;

			// Write timestamp (4 bytes, float)
			float timestamp = static_cast<float>(stopwatch.GetTime());
			file.write(reinterpret_cast<const char*>(&timestamp), sizeof(float));

			// Write packet length (2 bytes, uint16)
			uint16_t len = static_cast<uint16_t>(length);
			file.write(reinterpret_cast<const char*>(&len), sizeof(uint16_t));

			// Write packet data
			file.write(data, length);

			if (file.good()) {
				packetCount++;
			} else {
				SPLog("Failed to write packet to demo file");
			}
		}

		float DemoRecorder::GetRecordingTime() const {
			if (!recording)
				return 0.0f;
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

			std::string demosDir = GetDemosDirectory();

			// Ensure the Demos directory exists
			MKDIR(demosDir.c_str());

			std::ostringstream oss;
			oss << demosDir << "/" << std::put_time(tm, "%Y-%m-%d-%H-%M");
			if (!context.empty())
				oss << "-" << context;
			oss << ".dem";

			return oss.str();
		}

		static std::vector<std::string> ScanDemosDir() {
			std::string demosDir = DemoRecorder::GetDemosDirectory();
			std::vector<std::string> files;
#ifdef _WIN32
			WIN32_FIND_DATAA fd;
			HANDLE hFind = FindFirstFileA((demosDir + "\\*.dem").c_str(), &fd);
			if (hFind != INVALID_HANDLE_VALUE) {
				do {
					if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
						files.push_back(demosDir + "/" + fd.cFileName);
				} while (FindNextFileA(hFind, &fd));
				FindClose(hFind);
			}
#else
			DIR* dir = opendir(demosDir.c_str());
			if (dir) {
				struct dirent* entry;
				while ((entry = readdir(dir)) != nullptr) {
					std::string name = entry->d_name;
					if (name.size() > 4 && name.substr(name.size() - 4) == ".dem")
						files.push_back(demosDir + "/" + name);
				}
				closedir(dir);
			}
#endif
			std::sort(files.begin(), files.end());
			return files;
		}

		std::vector<std::string> DemoRecorder::ListRecordings() {
			return ScanDemosDir();
		}

		void DemoRecorder::PruneOldRecordings(size_t maxCount) {
			std::vector<std::string> files = ScanDemosDir();

			if (files.size() <= maxCount)
				return;

			size_t toRemove = files.size() - maxCount;
			for (size_t i = 0; i < toRemove; i++) {
				if (remove(files[i].c_str()) == 0) {
					SPLog("Pruned old demo recording: %s", files[i].c_str());
				} else {
					SPLog("Failed to prune demo recording: %s", files[i].c_str());
				}
			}
		}

	} // namespace client
} // namespace spades
