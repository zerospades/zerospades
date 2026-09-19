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

#include "KV6VoxelSelection.h"

#include <algorithm>

namespace spades {
	namespace gui {
		namespace {
			// A std::set node: the key, three tree links and the colour, as a
			// typical allocator rounds it. Only an estimate for memory budgets.
			constexpr std::size_t kSetNodeBytes = 48;
			constexpr int kKeyBits = 20;
			constexpr std::int64_t kKeyMask = (std::int64_t(1) << kKeyBits) - 1;
		} // namespace

		std::int64_t VoxelSelection::Key(const IntVector3& v) {
			return ((std::int64_t(v.x) & kKeyMask) << (2 * kKeyBits)) |
			       ((std::int64_t(v.y) & kKeyMask) << kKeyBits) | (std::int64_t(v.z) & kKeyMask);
		}

		IntVector3 VoxelSelection::Coordinates(std::int64_t key) {
			return IntVector3::Make(int((key >> (2 * kKeyBits)) & kKeyMask),
			                        int((key >> kKeyBits) & kKeyMask), int(key & kKeyMask));
		}

		void VoxelSelection::Remove(const IntVector3& v) {
			if (Contains(v)) // changing nothing must not unshare the set
				keys.Edit().erase(Key(v));
		}

		void VoxelSelection::Clear() {
			if (!Empty())
				keys = CopyOnWrite<std::set<std::int64_t>>();
		}

		void VoxelSelection::Shift(const IntVector3& offset) {
			if (offset == IntVector3::Make(0, 0, 0) || Empty())
				return;
			std::set<std::int64_t> shifted;
			ForEach([&](const IntVector3& v) { shifted.insert(Key(v + offset)); });
			keys = CopyOnWrite<std::set<std::int64_t>>(std::move(shifted));
		}

		bool VoxelSelection::Bounds(IntVector3& lo, IntVector3& hi) const {
			if (boundsVersion != keys.Version()) {
				boundsVersion = keys.Version();
				boundsValid = !Empty();
				if (boundsValid) {
					boundsLo = boundsHi = Coordinates(*keys->begin());
					ForEach([&](const IntVector3& v) {
						boundsLo = IntVector3::Make(std::min(boundsLo.x, v.x),
						                            std::min(boundsLo.y, v.y),
						                            std::min(boundsLo.z, v.z));
						boundsHi = IntVector3::Make(std::max(boundsHi.x, v.x),
						                            std::max(boundsHi.y, v.y),
						                            std::max(boundsHi.z, v.z));
					});
				}
			}
			lo = boundsLo;
			hi = boundsHi;
			return boundsValid;
		}

		std::size_t VoxelSelection::UnsharedBytes(const VoxelSelection& other) const {
			return keys.Shares(other.keys) ? 0 : keys->size() * kSetNodeBytes;
		}
	} // namespace gui
} // namespace spades
