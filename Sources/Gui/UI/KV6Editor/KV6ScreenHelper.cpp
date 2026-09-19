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

#include "KV6ScreenHelper.h"

#include <cstdio>

#include <Core/LocalFileSystem.h>
#include <Core/StdStream.h>
#include <Core/VoxelModel.h>
#include <Gui/Main.h>

namespace spades {
	namespace gui {
		namespace fs = LocalFileSystem;

		const std::vector<std::string>& KV6ModelExtensions() {
			// .kv6 is editable; .2kv6/.vxl are listed but not supported yet.
			static const std::vector<std::string> extensions = {".kv6", ".2kv6", ".vxl"};
			return extensions;
		}

		bool KV6IsEditable(const std::string& name) { return fs::HasExtension(name, ".kv6"); }

		KV6ScreenHelper::KV6ScreenHelper() {
			// Home is a dedicated `kv6/` folder inside the app-data dir (alongside
			// Mods/, Demos/, ...), created on demand. The parent already exists (the
			// game creates it at startup), so a single mkdir is enough.
			defaultDirAbs = fs::Join(std::string(spades::g_userResourceDirectory), "kv6");
			if (!fs::IsFolder(defaultDirAbs))
				fs::CreateFolder(defaultDirAbs);
		}

		KV6ScreenHelper::~KV6ScreenHelper() {}

		std::string KV6ScreenHelper::DefaultDir() {
			// Fall back to the app-data root if the kv6/ folder couldn't be created
			// (e.g. permissions, or the name is taken by a file), so the explorer
			// always opens somewhere valid.
			if (fs::IsFolder(defaultDirAbs))
				return defaultDirAbs;
			return std::string(spades::g_userResourceDirectory);
		}

		VoxelModel* KV6ScreenHelper::Load(const std::string& absPath) {
			std::FILE* f = fs::OpenFile(absPath, "rb");
			if (!f)
				return nullptr;
			try {
				StdStream stream(f, true); // takes ownership of the FILE*
				return VoxelModel::LoadKV6(stream).Unmanage();
			} catch (const std::exception&) {
				return nullptr;
			}
		}

		bool KV6ScreenHelper::Save(VoxelModel* model, const std::string& absPath) {
			if (!model)
				return false;
			// Write to a sibling temp file first, then atomically replace the target,
			// so a failure mid-write can never truncate or corrupt an existing model.
			std::string tmpPath = absPath + ".savetmp";
			std::FILE* f = fs::OpenFile(tmpPath, "wb");
			if (!f)
				return false;
			try {
				StdStream stream(f, true); // takes ownership; closes/flushes at scope exit
				model->SaveKV6(stream);
			} catch (const std::exception&) {
				fs::Delete(tmpPath);
				return false;
			}
			// The StdStream above is destroyed (closing the file) before we replace
			// the target, so the rename sees a fully written, flushed temp file.
			if (!fs::Rename(tmpPath, absPath, true)) {
				fs::Delete(tmpPath);
				return false;
			}
			return true;
		}

	} // namespace gui
} // namespace spades
