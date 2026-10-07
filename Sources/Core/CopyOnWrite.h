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
#include <memory>
#include <utility>

namespace spades {
	/**
	 * A value shared by its copies until one of them changes it.
	 *
	 * Copying one copies a pointer, so keeping snapshots of large state (an
	 * undo history holding a selection per step) costs nothing until the state
	 * changes; `Edit` first clones the value if anything else still shares it,
	 * so no copy ever sees another's changes. A default-constructed one holds
	 * `T()` without allocating, which also makes it safe to create where
	 * nothing may throw.
	 *
	 * Every change takes a new `Version`, and copies keep theirs, so equal
	 * versions always hold equal values: anything derived from a value can be
	 * cached against its version. For that to hold, take `Edit()` afresh for
	 * each change rather than keeping the reference across reads of a cache.
	 *
	 * Not thread-safe: a value and all its copies belong to one thread.
	 */
	template <class T> class CopyOnWrite {
	public:
		CopyOnWrite() noexcept = default;
		explicit CopyOnWrite(T value)
		    : value(std::make_shared<T>(std::move(value))), version(NextVersion()) {}

		const T& operator*() const noexcept { return value ? *value : Empty(); }
		const T* operator->() const noexcept { return &**this; }

		/** The value, to change; the change is private to this copy. */
		T& Edit() {
			if (!value)
				value = std::make_shared<T>();
			else if (value.use_count() > 1)
				value = std::make_shared<T>(*value);
			version = NextVersion();
			return *value;
		}

		/** Names the content: copies with equal versions hold equal values. */
		std::uint64_t Version() const noexcept { return version; }
		/** Whether both keep their value in the same place, so it is stored once. */
		bool Shares(const CopyOnWrite& o) const noexcept { return value == o.value; }

		bool operator==(const CopyOnWrite& o) const {
			return version == o.version || **this == *o;
		}
		bool operator!=(const CopyOnWrite& o) const { return !(*this == o); }

	private:
		std::shared_ptr<T> value; // null holds T()
		std::uint64_t version = 0; // 0 is T() as default-constructed

		static const T& Empty() noexcept {
			static const T empty{};
			return empty;
		}
		static std::uint64_t NextVersion() noexcept {
			static std::uint64_t next = 0;
			return ++next;
		}
	};
} // namespace spades
