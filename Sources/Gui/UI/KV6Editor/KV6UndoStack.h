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
#include <deque>
#include <set>
#include <string>
#include <vector>

#include <Core/Math.h>

#include "KV6EditState.h"

namespace spades {
	namespace gui {
		/**
		 * Bounded undo/redo history for the KV6 editor.
		 *
		 * The stack stores reversible deltas only and knows nothing about
		 * `VoxelModel`, the renderer or the camera: the host implements `Sink` to
		 * apply a delta to the live document, and the stack drives those callbacks in
		 * the correct order on Undo()/Redo(). This keeps the tricky bits (reverse
		 * replay, frame restoration, selection timing) in one place and lets the host
		 * stay a thin adapter.
		 *
		 * Each edit records into a `Step`, a scope that lives no longer than the
		 * call making the edit; nested steps coalesce, and a step is committed only
		 * if it actually changed the voxels or the rest of the edit state.
		 * Steps committed during one user action (a press-drag-release, a key
		 * press) then merge into a single undo step, so a paint stroke or a
		 * scripted multi-step edit undoes at once. Nothing stays open between
		 * events, so undo and redo work at any moment. The history is capped at
		 * `kMaxGroups`, evicting the oldest.
		 */
		class KV6UndoStack {
		public:
			/**
			 * Applies primitives to the live document. Every coordinate is in the
			 * document's current frame at the moment of the call; the stack guarantees
			 * frames are restored before the voxel deltas that depend on them.
			 */
			class Sink {
			public:
				virtual ~Sink() {}
				// Set voxel (x,y,z) solid with `color`, or air when `solid` is false.
				virtual void UndoApplyVoxel(int x, int y, int z, bool solid, uint32_t color) = 0;
				// Resize/relabel the volume to (w,h,d), shifting existing voxels by
				// (ox,oy,oz) — the same operation the editor uses to grow / trim.
				virtual void UndoApplyReframe(int w, int h, int d, int ox, int oy, int oz) = 0;
				// Set the model's origin (pivot = -origin) and rebuild what depends
				// on it. Used to replay a pivot change.
				virtual void UndoApplyOrigin(const Vector3& origin) = 0;
				// Read / replace everything besides the voxels that a step restores.
				virtual EditState UndoSnapshotState() const = 0;
				virtual void UndoRestoreState(const EditState& state) = 0;
				// Called once after a group has been applied, to refresh derived state
				// (the render model, etc.).
				virtual void UndoReplayed() = 0;
			};

			explicit KV6UndoStack(Sink& sink) : sink(sink) {}

			// --- recording ----------------------------------------------------
			/**
			 * One journaled edit, recorded for the lifetime of the scope. Nested
			 * steps coalesce into the outermost, which commits on leaving its scope
			 * (also when an exception unwinds it) if anything changed.
			 */
			class Step {
			public:
				Step(KV6UndoStack& stack, const std::string& label) : stack(stack) {
					stack.Begin(label);
				}
				~Step() { stack.End(); }
				Step(const Step&) = delete;
				Step& operator=(const Step&) = delete;

			private:
				KV6UndoStack& stack;
			};

			/**
			 * Starts a user action: every step committed until EndAction (or the
			 * next BeginAction, Undo, Redo or Clear) merges into one undo step.
			 * Actions hold nothing open, so ending one is never required for
			 * the history to work; it only stops the merging.
			 */
			void BeginAction();
			void EndAction() { action = 0; }

			// Append a reversible voxel change to the open group (old -> new state).
			void RecordVoxel(int x, int y, int z, bool oldSolid, uint32_t oldColor,
			                 bool newSolid, uint32_t newColor);
			// Append a volume reframe to the open group.
			void RecordReframe(int beforeW, int beforeH, int beforeD, int afterW, int afterH,
			                   int afterD, int ox, int oy, int oz);
			// Append a pivot (origin) change to the open group.
			void RecordOrigin(const Vector3& before, const Vector3& after);

			// --- history ------------------------------------------------------
			bool CanUndo() const { return !undoGroups.empty(); }
			bool CanRedo() const { return !redoGroups.empty(); }
			std::string UndoLabel() const { return undoGroups.empty() ? "" : undoGroups.back().label; }
			std::string RedoLabel() const { return redoGroups.empty() ? "" : redoGroups.back().label; }
			bool Undo(); // false if there was nothing to undo
			bool Redo();
			void Clear();

			// Monotonic id of the current *geometry* state, for the document's
			// dirty/clean flag. Selection-only steps leave it unchanged, so merely
			// selecting voxels never marks the document modified.
			long GeometryStateId() const { return geomId; }

		private:
			struct Record {
				enum class Kind : uint8_t { Voxel, Reframe, Origin } kind;
				union {
					struct {
						int x, y, z;
						uint32_t oldColor, newColor;
						bool oldSolid, newSolid;
					} v;
					struct {
						int bw, bh, bd, aw, ah, ad, ox, oy, oz;
					} r;
					struct {
						float bx, by, bz, ax, ay, az;
					} o;
				};
				static Record MakeVoxel(int x, int y, int z, bool oS, uint32_t oC, bool nS,
				                        uint32_t nC) {
					Record rec;
					rec.kind = Kind::Voxel;
					rec.v = {x, y, z, oC, nC, oS, nS};
					return rec;
				}
				static Record MakeReframe(int bw, int bh, int bd, int aw, int ah, int ad, int ox,
				                          int oy, int oz) {
					Record rec;
					rec.kind = Kind::Reframe;
					rec.r = {bw, bh, bd, aw, ah, ad, ox, oy, oz};
					return rec;
				}
				static Record MakeOrigin(const Vector3& b, const Vector3& a) {
					Record rec;
					rec.kind = Kind::Origin;
					rec.o = {b.x, b.y, b.z, a.x, a.y, a.z};
					return rec;
				}
			};
			struct Group {
				std::string label;
				std::vector<Record> records;
				EditState before, after;
				bool hasGeometry = false;
				long geomBefore = 0, geomAfter = 0;
				unsigned action = 0; // the user action it was recorded in, 0 for none
			};

			// Open / close the pending group; only through Step, so they pair up.
			void Begin(const std::string& label);
			void End();
			void Commit();
			void ApplyForward(const Group& g); // redo direction
			void ApplyInverse(const Group& g); // undo direction

			Sink& sink;
			std::deque<Group> undoGroups;
			std::deque<Group> redoGroups;
			Group pending;
			int depth = 0;
			unsigned action = 0, nextAction = 0;
			long geomId = 0, nextGeomId = 0;
			size_t totalRecords = 0; // deltas held across undo + redo (for the byte cap)

			static const size_t kMaxGroups = 256;
			// Cap the total deltas too, so one or many big edits can't grow the
			// history without bound (~240 MB at ~40 bytes/record). The most recent
			// step is always kept, even if it alone exceeds this.
			static const size_t kMaxRecords = 6000000;
		};
	} // namespace gui
} // namespace spades
