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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <Core/CopyOnWrite.h>
#include <Core/Math.h>

#include "KV6VoxelSelection.h"

namespace spades {
	namespace gui {
		/**
		 * Mirror modelling: which axes reflect edits, and the plane each reflects
		 * across. Not saved with the model, but it decides where edits land.
		 */
		struct MirrorSetup {
			bool enabled[3] = {false, false, false};
			Vector3 plane = MakeVector3(0.0F, 0.0F, 0.0F); // voxel coordinates

			bool operator==(const MirrorSetup& o) const {
				return enabled[0] == o.enabled[0] && enabled[1] == o.enabled[1] &&
				       enabled[2] == o.enabled[2] && plane == o.plane;
			}
			bool operator!=(const MirrorSetup& o) const { return !(*this == o); }
		};

		/** A voxel held apart from the document: its offset in a group, and colour. */
		struct ClipVoxel {
			IntVector3 rel;
			uint32_t color;

			bool operator==(const ClipVoxel& o) const { return rel == o.rel && color == o.color; }
		};

		/** Size of the box `voxels` fill from (0,0,0), at least one voxel each way. */
		inline IntVector3 ExtentOf(const std::vector<ClipVoxel>& voxels) {
			IntVector3 extent = IntVector3::Make(1, 1, 1);
			for (const ClipVoxel& v : voxels) {
				extent.x = std::max(extent.x, v.rel.x + 1);
				extent.y = std::max(extent.y, v.rel.y + 1);
				extent.z = std::max(extent.z, v.rel.z + 1);
			}
			return extent;
		}

		/**
		 * The voxel at the middle of the box [lo, hi]: the lower one where the
		 * middle falls between two. What a group of voxels turns about by default.
		 */
		inline IntVector3 MiddleOf(const IntVector3& lo, const IntVector3& hi) {
			return IntVector3::Make(lo.x + (hi.x - lo.x) / 2, lo.y + (hi.y - lo.y) / 2,
			                        lo.z + (hi.z - lo.z) / 2);
		}

		/**
		 * Voxels waiting to be placed: a paste, an import, or a selection lifted
		 * out of the document to be moved and turned. They are drawn where they
		 * would land, and only written into the document when they are placed,
		 * so dragging them over other voxels never destroys what they pass.
		 * They are only ever kept where they fit the model size limit, so
		 * placing them never fails.
		 *
		 * The voxel lists are shared between copies until changed, so an undo
		 * step that only moves them keeps no second copy of them.
		 */
		struct PendingPlacement {
			// Relative to `anchor`, filling a box from (0,0,0). Parallel to
			// `lifted` when that is not empty: voxel i was taken from lifted[i],
			// whatever turns it made since.
			CopyOnWrite<std::vector<ClipVoxel>> voxels;
			IntVector3 anchor = IntVector3::Make(0, 0, 0); // min corner, document coords
			// The middle of the voxels, in document coords, which turns go round
			// unless they go round the model's pivot. It follows the voxels
			// through every shift and turn, so turning back always returns them
			// exactly where they were.
			IntVector3 pivot = IntVector3::Make(0, 0, 0);
			// Where lifted voxels came from (empty for a paste or an import), so
			// cancelling puts them back.
			CopyOnWrite<std::vector<IntVector3>> lifted;
			std::string label = "Transform"; // names its undo steps

			/** ExtentOf(voxels), cached until the voxels change. */
			IntVector3 Extent() const {
				if (extentVersion != voxels.Version()) {
					extentVersion = voxels.Version();
					extent = ExtentOf(*voxels);
				}
				return extent;
			}

			/** Heap bytes this holds that `other` does not share with it. */
			std::size_t UnsharedBytes(const PendingPlacement& other) const {
				return (voxels.Shares(other.voxels) ? 0 : voxels->size() * sizeof(ClipVoxel)) +
				       (lifted.Shares(other.lifted) ? 0 : lifted->size() * sizeof(IntVector3));
			}

			bool operator==(const PendingPlacement& o) const {
				return anchor == o.anchor && pivot == o.pivot && voxels == o.voxels &&
				       lifted == o.lifted && label == o.label;
			}

		private:
			mutable std::uint64_t extentVersion = ~std::uint64_t(0); // none yet
			mutable IntVector3 extent = IntVector3::Make(1, 1, 1);
		};

		/**
		 * Everything besides the voxels that an undo step restores: what the
		 * user moved, turned or selected along with the model. Each step keeps
		 * this state from before and after it, so every such change undoes and
		 * redoes through the one history, whichever tool made it.
		 *
		 * The editor holds its live state as one of these, so a snapshot is a
		 * plain copy: state added here is journaled with no more code, and the
		 * copy is cheap because the large parts are shared until changed.
		 */
		struct EditState {
			VoxelSelection selection; // only ever solid voxels
			MirrorSetup mirror;
			bool placing = false;       // whether `placement` holds voxels
			PendingPlacement placement; // empty unless placing

			/** Heap bytes this holds that `other` does not share: its cost on top of it. */
			std::size_t UnsharedBytes(const EditState& other) const {
				return selection.UnsharedBytes(other.selection) +
				       (placing ? placement.UnsharedBytes(other.placement) : 0);
			}

			bool operator==(const EditState& o) const {
				return selection == o.selection && mirror == o.mirror && placing == o.placing &&
				       (!placing || placement == o.placement);
			}
			bool operator!=(const EditState& o) const { return !(*this == o); }
		};
	} // namespace gui
} // namespace spades
