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
#include <set>
#include <string>
#include <vector>

#include <Core/Math.h>

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

		/**
		 * Voxels waiting to be placed: a paste, an import, or a selection lifted
		 * out of the document to be moved and turned. They are drawn where they
		 * would land, and only written into the document when they are placed,
		 * so dragging them over other voxels never destroys what they pass.
		 * They are only ever kept where they fit the model size limit, so
		 * placing them never fails.
		 */
		struct PendingPlacement {
			// Relative to `anchor`. Parallel to `lifted` when that is not empty:
			// voxel i was taken from lifted[i], whatever turns it made since.
			std::vector<ClipVoxel> voxels;
			IntVector3 anchor = IntVector3::Make(0, 0, 0); // min corner, document coords
			// The middle of the voxels, in document coords, which turns go round
			// unless they go round the model's pivot. It follows the voxels
			// through every shift and turn, so turning back always returns them
			// exactly where they were.
			IntVector3 pivot = IntVector3::Make(0, 0, 0);
			// Where lifted voxels came from (empty for a paste or an import), so
			// cancelling puts them back.
			std::vector<IntVector3> lifted;
			std::string label = "Transform"; // names its undo steps

			bool operator==(const PendingPlacement& o) const {
				return anchor == o.anchor && pivot == o.pivot && voxels == o.voxels &&
				       lifted == o.lifted && label == o.label;
			}
		};

		/**
		 * Everything besides the voxels that an undo step restores: what the
		 * user moved, turned or selected along with the model. Each step keeps
		 * this state from before and after it, so every such change undoes and
		 * redoes through the one history, whichever tool made it.
		 */
		struct EditState {
			std::set<int64_t> selection; // packed voxel keys
			MirrorSetup mirror;
			bool placing = false; // whether `placement` holds voxels
			PendingPlacement placement;

			bool operator==(const EditState& o) const {
				return selection == o.selection && mirror == o.mirror && placing == o.placing &&
				       (!placing || placement == o.placement);
			}
			bool operator!=(const EditState& o) const { return !(*this == o); }
		};
	} // namespace gui
} // namespace spades
