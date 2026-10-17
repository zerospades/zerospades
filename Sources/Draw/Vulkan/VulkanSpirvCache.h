/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

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

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>

namespace spades {
	namespace draw {
		// Process-wide cache of SPIR-V blobs keyed by resource path. Lets
		// PreloadShaders() pull shader binaries off disk at renderer init so
		// later pipeline builds (map load / first draw) skip file IO.
		namespace SpirvCache {

			inline std::mutex& GetMutex() {
				static std::mutex m;
				return m;
			}

			inline std::unordered_map<std::string, std::vector<uint32_t>>& GetMap() {
				static std::unordered_map<std::string, std::vector<uint32_t>> map;
				return map;
			}

			inline std::vector<uint32_t> Load(const std::string& filename) {
				{
					std::lock_guard<std::mutex> lock{GetMutex()};
					auto it = GetMap().find(filename);
					if (it != GetMap().end())
						return it->second;
				}

				std::unique_ptr<IStream> stream = FileManager::OpenForReading(filename.c_str());
				if (!stream) {
					SPRaise("Failed to open shader file: %s", filename.c_str());
				}
				size_t size = stream->GetLength();
				std::vector<uint32_t> code(size / 4);
				stream->Read(code.data(), size);

				{
					std::lock_guard<std::mutex> lock{GetMutex()};
					GetMap().emplace(filename, code);
				}
				return code;
			}

			// Best-effort warm-up; missing files are logged, not fatal.
			inline void Preload(std::initializer_list<const char*> files) {
				for (const char* f : files) {
					try {
						Load(f);
					} catch (const std::exception& ex) {
						SPLog("SpirvCache: failed to preload '%s': %s", f, ex.what());
					}
				}
			}

		} // namespace SpirvCache
	} // namespace draw
} // namespace spades
