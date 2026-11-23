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

#include <string>

#include <Core/RefCountedObject.h>

namespace spades {
	class VoxelModel;
	namespace gui {
		/**
		 * Reads and writes KV6 models by absolute path: what is specific to KV6
		 * documents. Browsing and other file operations belong to
		 * `LocalFileSystem`.
		 */
		namespace KV6ModelIO {
			/** The model at `absPath`, or null when it cannot be read (missing,
			 *  corrupt); never throws. */
			Handle<VoxelModel> Load(const std::string& absPath);

			/** Writes `model` to `absPath` through a temporary file, so a failure
			 *  can never truncate what was there; false on failure. */
			bool Save(VoxelModel& model, const std::string& absPath);
		} // namespace KV6ModelIO
	} // namespace gui
} // namespace spades
