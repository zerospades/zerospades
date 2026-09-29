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

#include <Core/Math.h>

namespace spades {
	namespace gui {
		/**
		 * The voxels a voxel editor edits: a box of cells, each solid with a
		 * colour or air. The editor (VoxelEditor) journals, mirrors, selects and
		 * places on top of this; a document only stores and reports.
		 *
		 * Coordinates are voxel indices, 0 .. Size() - 1 on each axis. Colours
		 * are packed 0x00BBGGRR.
		 */
		class IVoxelDocument {
		public:
			virtual ~IVoxelDocument() {}

			/** What the document is called in messages: "model", "map". */
			virtual std::string Noun() const = 0;

			// --- Extent -------------------------------------------------------
			virtual IntVector3 Size() const = 0;
			/**
			 * Whether edits relabel the volume: grow it to hold what lands past
			 * it, and trim it to the voxels after a removal. A document that
			 * cannot keeps its size, and edits past it are clipped.
			 */
			virtual bool Resizable() const = 0;
			/** The largest the volume may become; Size() when not Resizable. */
			virtual IntVector3 MaxSize() const = 0;
			/** Resizes to `size`, moving every voxel by `shift`. Resizable only. */
			virtual void Reframe(const IntVector3& size, const IntVector3& shift) = 0;

			// --- Voxels -------------------------------------------------------
			/** False outside the volume. */
			virtual bool IsSolid(int x, int y, int z) const = 0;
			/**
			 * Whether edits may change a voxel inside the volume. One that may not
			 * (a map's bedrock) can still be aimed at and built on, but is never
			 * written, selected, erased or painted.
			 */
			virtual bool IsEditable(int x, int y, int z) const = 0;
			/** The colour of a voxel inside the volume. */
			virtual std::uint32_t Color(int x, int y, int z) const = 0;
			/** Sets a voxel inside the volume; not journaled (VoxelEditor does). */
			virtual void Write(int x, int y, int z, bool solid, std::uint32_t color) = 0;
			/**
			 * How many voxels may still be removed: a model keeps at least one,
			 * a map none. `RemovalLimitMessage` says why when it is reached.
			 */
			virtual int RemovableVoxels() const = 0;
			virtual std::string RemovalLimitMessage() const = 0;

			// --- Origin -------------------------------------------------------
			/** The document's origin; its pivot is at -Origin(). */
			virtual Vector3 Origin() const = 0;
			virtual void SetOrigin(const Vector3& origin) = 0;

			/** Advances with every change above, so derived data can be keyed on it. */
			virtual std::uint64_t Version() const = 0;
		};
	} // namespace gui
} // namespace spades
