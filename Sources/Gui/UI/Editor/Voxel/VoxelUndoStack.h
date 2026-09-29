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

#include <Core/Math.h>
#include <Gui/UI/Editor/Shell/UndoHistory.h>

#include "EditState.h"

namespace spades {
	namespace gui {
		/**
		 * One reversible change of a voxel document: a voxel set or cleared, the
		 * volume resized and relabelled, or the pivot moved. Plain data, as a
		 * stroke records thousands of them.
		 */
		struct VoxelUndoRecord {
			enum class Kind : std::uint8_t { Voxel, Reframe, Origin } kind;
			union {
				struct {
					int x, y, z;
					std::uint32_t oldColor, newColor;
					bool oldSolid, newSolid;
				} v;
				struct {
					int bw, bh, bd, aw, ah, ad, ox, oy, oz;
				} r;
				struct {
					float bx, by, bz, ax, ay, az;
				} o;
			};
		};

		/**
		 * The undo history of a voxel document (see UndoHistory), recording
		 * voxels, reframes and pivot moves, with the rest of the edit state
		 * (selection, mirror, pending placement) snapshotted as an EditState.
		 */
		class VoxelUndoStack : public UndoHistory<VoxelUndoRecord, EditState> {
		public:
			/**
			 * Applies the records to the live document. Every coordinate is in the
			 * document's current frame at the moment of the call; the history
			 * replays in order, so frames are restored before the voxel changes
			 * that depend on them.
			 */
			class Sink : public UndoHistory<VoxelUndoRecord, EditState>::Sink {
			public:
				// Set voxel (x,y,z) solid with `color`, or air when `solid` is false.
				virtual void UndoApplyVoxel(int x, int y, int z, bool solid, std::uint32_t color) = 0;
				// Resize/relabel the volume to (w,h,d), shifting existing voxels by
				// (ox,oy,oz) — the same operation the editor uses to grow / trim.
				virtual void UndoApplyReframe(int w, int h, int d, int ox, int oy, int oz) = 0;
				// Set the model's origin (pivot = -origin) and rebuild what depends
				// on it. Used to replay a pivot change.
				virtual void UndoApplyOrigin(const Vector3& origin) = 0;

				void UndoApply(const VoxelUndoRecord& record, bool forward) final;
			};

			explicit VoxelUndoStack(Sink& sink, Limits limits = Limits())
			    : UndoHistory(sink, limits) {}

			// Append a voxel change to the step open (old -> new state); a change
			// that changes nothing is not recorded.
			void RecordVoxel(int x, int y, int z, bool oldSolid, std::uint32_t oldColor,
			                 bool newSolid, std::uint32_t newColor);
			// Append a volume reframe to the step open.
			void RecordReframe(int beforeW, int beforeH, int beforeD, int afterW, int afterH,
			                   int afterD, int ox, int oy, int oz);
			// Append a pivot (origin) change to the step open; the pivot is saved
			// with the model, so it is a change of the document.
			void RecordOrigin(const Vector3& before, const Vector3& after);
		};
	} // namespace gui
} // namespace spades
