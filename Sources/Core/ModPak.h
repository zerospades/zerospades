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

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace spades {
	class IFileSystem;
	class IStream;
	class ZipFileSystem;

	/**
	 * A pak of a mod in Mods/, read into memory so the file on disk is never held
	 * open and can be replaced while the game runs. It can be mounted over the
	 * base paks (FileManager), or read through a FileManager::Overlay.
	 */
	class ModPak {
		std::string mod;
		std::string id; // path and checksum: equal ids, equal files
		std::unique_ptr<IStream> stream;        // outlives the file system
		std::unique_ptr<IFileSystem> unmounted; // owns `fs` while not mounted
		ZipFileSystem* fs;

		ModPak();

	public:
		~ModPak();

		/**
		 * The paks of the mod named `name`, a pak in Mods/ or a folder of paks
		 * there, as resource paths in mount order. Empty if there is no such mod.
		 */
		static std::vector<std::string> FindPaths(const std::string& name);

		/** Reads the pak at `path`, a resource path, of the mod `mod`. Raises if it can't. */
		static std::unique_ptr<ModPak> Load(const std::string& path, const std::string& mod);

		const std::string& GetMod() const { return mod; }
		const std::string& GetId() const { return id; }
		std::vector<std::string> GetFiles() const;
		/** Its file system: valid while the pak lives, mounted or not. */
		IFileSystem* GetFileSystem() const;

		/** Puts it over every file system mounted so far. */
		void Mount();
		/** Takes it down again. */
		void Unmount();
	};
} // namespace spades
