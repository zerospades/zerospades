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

#include <cstddef>
#include <cstdint>
#include <set>

#include <Core/CopyOnWrite.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		/**
		 * A set of voxel coordinates: the voxels the user has selected.
		 *
		 * A value type, cheap to copy: copies share the coordinates until one
		 * changes (see CopyOnWrite), so every undo step can keep one. Its box
		 * is cached until the selection next changes, so asking for it every
		 * frame costs nothing. Coordinates are non-negative voxel indices below
		 * 2^20 on each axis, as a model's are.
		 */
		class VoxelSelection {
		public:
			bool Empty() const { return keys->empty(); }
			int Size() const { return int(keys->size()); }
			bool Contains(const IntVector3& v) const { return keys->count(Key(v)) != 0; }

			void Add(const IntVector3& v) {
				if (!Contains(v)) // changing nothing must not unshare the set
					keys.Edit().insert(Key(v));
			}
			void Remove(const IntVector3& v);
			void Clear();
			/** Moves every coordinate by `offset`, as the volume's are relabelled. */
			void Shift(const IntVector3& offset);

			/** The smallest box holding every voxel; false when there are none. */
			bool Bounds(IntVector3& lo, IntVector3& hi) const;

			/** Calls `visit(const IntVector3&)` for each voxel. */
			template <class Visit> void ForEach(Visit visit) const {
				for (std::int64_t key : *keys)
					visit(Coordinates(key));
			}

			/** Heap bytes this holds that `other` does not share with it. */
			std::size_t UnsharedBytes(const VoxelSelection& other) const;

			bool operator==(const VoxelSelection& o) const { return keys == o.keys; }
			bool operator!=(const VoxelSelection& o) const { return !(*this == o); }

		private:
			CopyOnWrite<std::set<std::int64_t>> keys;

			// The box as of `boundsVersion` of `keys`. Copies carry it along, and
			// share its validity since they share the version.
			mutable std::uint64_t boundsVersion = ~std::uint64_t(0); // none yet
			mutable bool boundsValid = false;
			mutable IntVector3 boundsLo = IntVector3::Make(0, 0, 0);
			mutable IntVector3 boundsHi = IntVector3::Make(0, 0, 0);

			static std::int64_t Key(const IntVector3& v);
			static IntVector3 Coordinates(std::int64_t key);
		};
	} // namespace gui
} // namespace spades
