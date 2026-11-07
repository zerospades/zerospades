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
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Components/Gizmo/GizmoView.h>

namespace spades {
	class VoxelModel;
	namespace client {
		class IRenderer;
	}
	namespace gui {
		class IVoxelDocument;

		/**
		 * The 12 edges of the axis-aligned box spanning corners `a`..`b`, handed
		 * one at a time to `emit(p, q)`.
		 */
		template <class Emit> void ForEachBoxEdge(const Vector3& a, const Vector3& b, Emit emit) {
			for (int k = 0; k < 2; k++) {
				float z = (k == 0) ? a.z : b.z;
				emit(MakeVector3(a.x, a.y, z), MakeVector3(b.x, a.y, z));
				emit(MakeVector3(b.x, a.y, z), MakeVector3(b.x, b.y, z));
				emit(MakeVector3(b.x, b.y, z), MakeVector3(a.x, b.y, z));
				emit(MakeVector3(a.x, b.y, z), MakeVector3(a.x, a.y, z));
			}
			emit(MakeVector3(a.x, a.y, a.z), MakeVector3(a.x, a.y, b.z));
			emit(MakeVector3(b.x, a.y, a.z), MakeVector3(b.x, a.y, b.z));
			emit(MakeVector3(b.x, b.y, a.z), MakeVector3(b.x, b.y, b.z));
			emit(MakeVector3(a.x, b.y, a.z), MakeVector3(a.x, b.y, b.z));
		}

		/**
		 * A voxel editor's overlay lines (outlines, tool wires), drawn over the
		 * finished scene as 2D strokes: a dark casing under a coloured core
		 * where in view, a dim core where voxels hide them.
		 *
		 * Visibility is worked out against the voxels rather than left to the
		 * depth buffer, as outlines lie on voxel faces and would fight them for
		 * depth, and a 3D line cannot be given a casing or a width. That is the
		 * costly part, so what it works out is kept while the lines, the view
		 * and the voxels hold still.
		 */
		class VoxelOverlayLines {
		public:
			/** What hides the lines: the document, and voxels waiting to be
			 *  placed, rendered solid where they would land. */
			struct Occluders {
				const IVoxelDocument* document = nullptr;
				// The waiting voxels' own grid, its (0,0,0) at `pendingAnchor` in
				// the document; null when none wait.
				const VoxelModel* pending = nullptr;
				IntVector3 pendingAnchor = IntVector3::Make(0, 0, 0);
				// Name the content above: equal versions, equal occluders.
				std::uint64_t documentVersion = 0;
				std::uint64_t pendingVersion = 0;
			};

			/** Queues a line for this frame. */
			void Emit(const Vector3& a, const Vector3& b, const Vector4& color);
			/** Draws this frame's lines as seen through `view`, then forgets them. */
			void Draw(client::IRenderer& renderer, const GizmoView& view,
			          const Occluders& occluders);

		private:
			struct Line {
				Vector3 a, b;
				Vector4 color;
			};
			// A piece of a line on screen, in view or hidden throughout.
			struct Stroke {
				Vector2 a, b;
				float widthScale;
				Vector4 color;
			};

			std::vector<Line> lines; // this frame's, as emitted

			// What Update worked out, and what it depends on.
			struct Cache {
				std::vector<Line> emitted; // as emitted, to spot a change
				std::vector<Line> lines;   // the same, each once
				GizmoView view;
				std::uint64_t documentVersion = 0;
				bool hasPending = false;
				std::uint64_t pendingVersion = 0;
				IntVector3 pendingAnchor = IntVector3::Make(0, 0, 0);
				bool valid = false; // strokes match everything above
				std::vector<Stroke> hidden, shown;
			} cache;

			/** This frame's lines -> cached strokes. */
			void Update(const GizmoView& view, const Occluders& occluders);
		};
	} // namespace gui
} // namespace spades
