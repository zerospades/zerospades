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
#include <map>

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
		 * 2^20 across and 2^20 deep, as a model's and a map's are.
		 *
		 * Stored as columns of 64 voxels, one bit each, the way a model and a
		 * map store their own voxels: a selection as large as a map is a few
		 * megabytes, not a node per voxel.
		 */
		class VoxelSelection {
		public:
			bool Empty() const { return columns->count == 0; }
			int Size() const { return columns->count; }
			bool Contains(const IntVector3& v) const;

			void Add(const IntVector3& v);
			void Remove(const IntVector3& v);
			void Clear();
			/** Moves every coordinate by `offset`, as the volume's are relabelled. */
			void Shift(const IntVector3& offset);

			/** The smallest box holding every voxel; false when there are none. */
			bool Bounds(IntVector3& lo, IntVector3& hi) const;

			/** Calls `visit(const IntVector3&)` for each voxel, column by column. */
			template <class Visit> void ForEach(Visit visit) const {
				for (const auto& column : columns->bits) {
					const IntVector3 base = ColumnBase(column.first);
					std::uint64_t bits = column.second;
					while (bits) {
						const int z = LowestBit(bits);
						visit(IntVector3::Make(base.x, base.y, base.z + z));
						bits &= bits - 1;
					}
				}
			}

			/** Heap bytes this holds that `other` does not share with it. */
			std::size_t UnsharedBytes(const VoxelSelection& other) const;

			/** Names the content: selections with equal versions hold the same
			 *  voxels, so what is worked out from one can be kept against it. */
			std::uint64_t Version() const { return columns.Version(); }

			bool operator==(const VoxelSelection& o) const { return columns == o.columns; }
			bool operator!=(const VoxelSelection& o) const { return !(*this == o); }

		private:
			// Each column holds 64 voxels of one (x, y), from a z that is a
			// multiple of 64: bit i is z + i.
			struct Columns {
				std::map<std::int64_t, std::uint64_t> bits; // column key -> voxels
				int count = 0;                              // voxels, all columns
				bool operator==(const Columns& o) const { return bits == o.bits; }
			};
			CopyOnWrite<Columns> columns;

			// The box as of `boundsVersion` of `columns`. Copies carry it along, and
			// share its validity since they share the version.
			mutable std::uint64_t boundsVersion = ~std::uint64_t(0); // none yet
			mutable bool boundsValid = false;
			mutable IntVector3 boundsLo = IntVector3::Make(0, 0, 0);
			mutable IntVector3 boundsHi = IntVector3::Make(0, 0, 0);

			static std::int64_t ColumnKey(const IntVector3& v);
			// The voxel bit 0 of the column `key` stands for.
			static IntVector3 ColumnBase(std::int64_t key);
			static int LowestBit(std::uint64_t bits);
		};
	} // namespace gui
} // namespace spades
