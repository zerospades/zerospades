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
#include <string>

#include <Core/RefCountedObject.h>
#include <ScriptBindings/ScriptManager.h>

#include <AngelScript/addons/scriptarray.h>

namespace spades {
	class VoxelModel;
	namespace gui {

		/**
		 * Backend for the KV6 editor's file explorer.
		 *
		 * All paths are absolute, so the explorer can browse the whole
		 * filesystem (not just the game's data directory). KV6 files are loaded
		 * and saved directly by absolute path, bypassing the FileManager mounts.
		 */
		class KV6ScreenHelper : public RefCountedObject {
		public:
			KV6ScreenHelper();

			// Subfolder names directly under `absDir` (sorted, case-insensitive).
			CScriptArray* GetFolders(const std::string& absDir);
			// Model file names (.kv6/.2kv6/.vxl) under `absDir` (sorted).
			CScriptArray* GetFiles(const std::string& absDir);

			bool Exists(const std::string& absPath);
			bool IsFolder(const std::string& absPath);
			int64_t GetFileSize(const std::string& absPath);

			bool CreateFolder(const std::string& absPath);
			bool Delete(const std::string& absPath);
			bool Rename(const std::string& absOld, const std::string& absNew);

			// The default folder to open in (absolute): the data dir's kv6/.
			std::string DefaultDir();
			// The parent directory of `absPath` (stops at the filesystem root).
			std::string ParentDir(const std::string& absPath);

			// Load / save a KV6 model by absolute path. `Load` returns null on
			// failure (e.g. missing or corrupt file) rather than throwing.
			VoxelModel* Load(const std::string& absPath);
			bool Save(VoxelModel* model, const std::string& absPath);

		protected:
			~KV6ScreenHelper();

		private:
			std::string defaultDirAbs; // <user data dir>/kv6
		};

	} // namespace gui
} // namespace spades
