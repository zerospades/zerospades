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

#include "VoxelSelection.h"

#include <algorithm>

#include <Core/Debug.h>

namespace spades {
	namespace gui {
		namespace {
			// A std::map node: the key, the column, three tree links and the
			// colour, as a typical allocator rounds it. Only an estimate for
			// memory budgets.
			constexpr std::size_t kMapNodeBytes = 48;
			constexpr int kCoordBits = 20;
			constexpr std::int64_t kCoordMask = (std::int64_t(1) << kCoordBits) - 1;
			constexpr int kColumnBits = 6; // 64 voxels a column
			constexpr int kColumnSize = 1 << kColumnBits;
			// z / 64 fits in the bits left under y.
			constexpr int kChunkBits = kCoordBits - kColumnBits;
			constexpr std::int64_t kChunkMask = (std::int64_t(1) << kChunkBits) - 1;

			std::uint64_t Bit(const IntVector3& v) {
				return std::uint64_t(1) << (v.z & (kColumnSize - 1));
			}
		} // namespace

		std::int64_t VoxelSelection::ColumnKey(const IntVector3& v) {
			SPAssert(v.x >= 0 && v.y >= 0 && v.z >= 0);
			return ((std::int64_t(v.x) & kCoordMask) << (kCoordBits + kChunkBits)) |
			       ((std::int64_t(v.y) & kCoordMask) << kChunkBits) |
			       (std::int64_t(v.z >> kColumnBits) & kChunkMask);
		}

		IntVector3 VoxelSelection::ColumnBase(std::int64_t key) {
			return IntVector3::Make(int((key >> (kCoordBits + kChunkBits)) & kCoordMask),
			                        int((key >> kChunkBits) & kCoordMask),
			                        int(key & kChunkMask) << kColumnBits);
		}

		int VoxelSelection::LowestBit(std::uint64_t bits) {
			int n = 0;
			while (!(bits & 1)) {
				bits >>= 1;
				n++;
			}
			return n;
		}

		bool VoxelSelection::Contains(const IntVector3& v) const {
			if (v.x < 0 || v.y < 0 || v.z < 0)
				return false;
			const auto it = columns->bits.find(ColumnKey(v));
			return it != columns->bits.end() && (it->second & Bit(v)) != 0;
		}

		void VoxelSelection::Add(const IntVector3& v) {
			if (Contains(v)) // changing nothing must not unshare the columns
				return;
			Columns& c = columns.Edit();
			c.bits[ColumnKey(v)] |= Bit(v);
			c.count++;
		}

		void VoxelSelection::Remove(const IntVector3& v) {
			if (!Contains(v)) // changing nothing must not unshare the columns
				return;
			Columns& c = columns.Edit();
			const auto it = c.bits.find(ColumnKey(v));
			it->second &= ~Bit(v);
			if (it->second == 0)
				c.bits.erase(it);
			c.count--;
		}

		void VoxelSelection::Clear() {
			if (!Empty())
				columns = CopyOnWrite<Columns>();
		}

		void VoxelSelection::Shift(const IntVector3& offset) {
			if (offset == IntVector3::Make(0, 0, 0) || Empty())
				return;
			Columns shifted;
			if (offset.z % kColumnSize == 0) {
				// Whole columns move: only their keys change.
				for (const auto& column : columns->bits)
					shifted.bits.emplace(ColumnKey(ColumnBase(column.first) + offset),
					                     column.second);
				shifted.count = columns->count;
			} else {
				ForEach([&](const IntVector3& v) {
					const IntVector3 moved = v + offset;
					shifted.bits[ColumnKey(moved)] |= Bit(moved);
				});
				shifted.count = columns->count;
			}
			columns = CopyOnWrite<Columns>(std::move(shifted));
		}

		bool VoxelSelection::Bounds(IntVector3& lo, IntVector3& hi) const {
			if (boundsVersion != columns.Version()) {
				boundsVersion = columns.Version();
				boundsValid = !Empty();
				bool first = true;
				for (const auto& column : columns->bits) {
					const IntVector3 base = ColumnBase(column.first);
					int top = 0, bottom = kColumnSize - 1;
					while (!((column.second >> top) & 1))
						top++;
					while (!((column.second >> bottom) & 1))
						bottom--;
					const IntVector3 a = IntVector3::Make(base.x, base.y, base.z + top);
					const IntVector3 b = IntVector3::Make(base.x, base.y, base.z + bottom);
					if (first) {
						boundsLo = a;
						boundsHi = b;
						first = false;
						continue;
					}
					boundsLo = IntVector3::Make(std::min(boundsLo.x, a.x), std::min(boundsLo.y, a.y),
					                            std::min(boundsLo.z, a.z));
					boundsHi = IntVector3::Make(std::max(boundsHi.x, b.x), std::max(boundsHi.y, b.y),
					                            std::max(boundsHi.z, b.z));
				}
			}
			lo = boundsLo;
			hi = boundsHi;
			return boundsValid;
		}

		std::size_t VoxelSelection::UnsharedBytes(const VoxelSelection& other) const {
			return columns.Shares(other.columns) ? 0 : columns->bits.size() * kMapNodeBytes;
		}
	} // namespace gui
} // namespace spades
