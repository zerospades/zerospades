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

#include "KV6ModelIO.h"

#include <cstdio>

#include <Core/LocalFileSystem.h>
#include <Core/StdStream.h>
#include <Core/VoxelModel.h>

namespace spades {
	namespace gui {
		namespace fs = LocalFileSystem;

		Handle<VoxelModel> KV6ModelIO::Load(const std::string& absPath) {
			std::FILE* f = fs::OpenFile(absPath, "rb");
			if (!f)
				return Handle<VoxelModel>();
			try {
				StdStream stream(f, true); // takes ownership of the FILE*
				return VoxelModel::LoadKV6(stream);
			} catch (const std::exception&) {
				return Handle<VoxelModel>();
			}
		}

		bool KV6ModelIO::Save(VoxelModel& model, const std::string& absPath) {
			// Write to a sibling temp file first, then atomically replace the target,
			// so a failure mid-write can never truncate or corrupt an existing model.
			std::string tmpPath = absPath + ".savetmp";
			std::FILE* f = fs::OpenFile(tmpPath, "wb");
			if (!f)
				return false;
			try {
				StdStream stream(f, true); // takes ownership; closes/flushes at scope exit
				model.SaveKV6(stream);
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
