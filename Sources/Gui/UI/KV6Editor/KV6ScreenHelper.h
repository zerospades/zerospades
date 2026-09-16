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
#include <vector>

#include <Core/RefCountedObject.h>

namespace spades {
	class VoxelModel;
	namespace gui {
		/** Extensions of the voxel model files the editor lists. */
		const std::vector<std::string>& KV6ModelExtensions();
		/** True if `name` is a directly editable model file (currently `.kv6`). */
		bool KV6IsEditable(const std::string& name);

		/**
		 * Loads and saves KV6 models by absolute path, and names the folder the
		 * editor opens in.
		 *
		 * Browsing and other file operations belong to `LocalFileSystem`; this only
		 * covers what is specific to KV6 documents.
		 */
		class KV6ScreenHelper : public RefCountedObject {
		public:
			KV6ScreenHelper();

			/** The folder to open in (absolute): the data dir's kv6/. */
			std::string DefaultDir();

			/**
			 * Load / save a KV6 model by absolute path. `Load` returns null on
			 * failure (e.g. missing or corrupt file) rather than throwing; `Save`
			 * writes through a temp file so a failure cannot truncate the target.
			 */
			VoxelModel* Load(const std::string& absPath);
			bool Save(VoxelModel* model, const std::string& absPath);

		protected:
			~KV6ScreenHelper();

		private:
			std::string defaultDirAbs; // <user data dir>/kv6
		};
	} // namespace gui
} // namespace spades
