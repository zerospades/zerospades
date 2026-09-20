/*
 Copyright (c) 2019 yvt

 This file is part of OpenSpades.

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

#include <string>

#include <Core/RefCountedObject.h>

namespace spades {
	class VoxelModel;

	/**
	 * Provides a method for loading `VoxelModel` augmented by metadata.
	 */
	class VoxelModelLoader final {
		VoxelModelLoader() = delete;

	public:
		/**
		 * Load a `VoxelModel` from the specified `FileManager`-style path.
		 *
		 * Supports both .kv6 (single model) and .2kv6 (scene) formats.
		 * For .2kv6 files, all objects in the scene are composited into a single model
		 * with proper transforms applied. This ensures seamless rendering as game assets.
		 *
		 * In addition to loading the file at `path`, this method also loads
		 * metadata from `BASENAME.meta.json` (where `BASENAME` is a portion of
		 * `path` without a file extension).
		 *
		 * All supported fields of a metadata file are shown below:
		 *
		 *     {
		 *       // Override the origin point
		 *       "Origin": [0.3, 0.5, 0.7],
		 *
		 *       // Replace the material ID
		 *       "ForceMaterial": "Default",
		 *       "ForceMaterial": "Emissive",
		 *
		 *       // (All fields are optional)
		 *     }
		 */
		static Handle<VoxelModel> Load(const char* path);
	};
} // namespace spades