/*
 Copyright (c) 2026 ZeroSpades developers.

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
#include <cctype>
#include <cstdint>
#include <limits>

#include <zlib.h>

#include "ModPak.h"
#include "Debug.h"
#include "DynamicMemoryStream.h"
#include "FileManager.h"
#include "TMPUtils.h"
#include "ZipFileSystem.h"

namespace spades {
	namespace {
		bool IsPak(const std::string& name) {
			if (name.size() < 4)
				return false;
			std::string ext = name.substr(name.size() - 4);
			for (char& c : ext)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return ext == ".pak" || ext == ".zip";
		}

		std::uint32_t Checksum(const std::string& bytes) {
			uLong crc = crc32(0, nullptr, 0);
			const auto* data = reinterpret_cast<const Bytef*>(bytes.data());
			std::size_t left = bytes.size();
			while (left > 0) {
				auto n = static_cast<uInt>(
				  std::min<std::size_t>(left, std::numeric_limits<uInt>::max()));
				crc = crc32(crc, data, n);
				data += n;
				left -= n;
			}
			return static_cast<std::uint32_t>(crc);
		}
	} // namespace

	ModPak::ModPak() : fs(nullptr) {}
	// `unmounted` goes before `stream`, which the file system reads from.
	ModPak::~ModPak() {}

	std::vector<std::string> ModPak::FindPaths(const std::string& name) {
		SPADES_MARK_FUNCTION();

		if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos ||
		    name.find('\\') != std::string::npos)
			return {};

		std::string path = "Mods/" + name;
		if (IsPak(name))
			return FileManager::FileExists(path.c_str()) ? std::vector<std::string>{path}
			                                             : std::vector<std::string>{};

		std::vector<std::string> paks;
		for (const std::string& file : FileManager::EnumFiles(path.c_str())) {
			if (IsPak(file))
				paks.push_back(path + "/" + file);
		}
		std::sort(paks.begin(), paks.end());
		return paks;
	}

	std::unique_ptr<ModPak> ModPak::Load(const std::string& path, const std::string& mod) {
		SPADES_MARK_FUNCTION();

		std::string bytes = FileManager::ReadAllBytes(path.c_str());
		auto stream = stmp::make_unique<DynamicMemoryStream>();
		stream->Write(bytes.data(), bytes.size());
		stream->SetPosition(0);

		std::unique_ptr<ModPak> pak{new ModPak()};
		pak->mod = mod;
		pak->id = path + "#" + std::to_string(Checksum(bytes));
		pak->fs = new ZipFileSystem(stream.get());
		pak->unmounted.reset(pak->fs);
		pak->stream = std::move(stream);
		return pak;
	}

	std::vector<std::string> ModPak::GetFiles() const { return fs->GetAllFiles(); }

	IFileSystem* ModPak::GetFileSystem() const { return fs; }

	void ModPak::Mount() {
		if (unmounted)
			FileManager::PrependFileSystem(unmounted.release());
	}

	void ModPak::Unmount() {
		if (!unmounted)
			unmounted = FileManager::RemoveFileSystem(fs);
	}
} // namespace spades
