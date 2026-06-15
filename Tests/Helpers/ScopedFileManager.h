/*
 Copyright (c) 2013 yvt

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

#include <Core/FileManager.h>

namespace spades {
	namespace tests {

		/**
		 * RAII guard that removes all FileManager providers on construction.
		 *
		 * Constructor calls FileManager::Close() unconditionally — this is the only
		 * API for clearing the provider list and is correct for logic tests, which
		 * construct their maps from MakeFlatMapBytes() entirely in memory and never
		 * call FileManager themselves.
		 *
		 * Destructor is a no-op: tests add no providers, so there is nothing to restore.
		 *
		 * Non-copyable, non-moveable.
		 */
		class ScopedFileManager {
		public:
			ScopedFileManager() { FileManager::Close(); }
			~ScopedFileManager() {}

			ScopedFileManager(const ScopedFileManager&) = delete;
			ScopedFileManager& operator=(const ScopedFileManager&) = delete;
			ScopedFileManager(ScopedFileManager&&) = delete;
			ScopedFileManager& operator=(ScopedFileManager&&) = delete;
		};

	} // namespace tests
} // namespace spades
