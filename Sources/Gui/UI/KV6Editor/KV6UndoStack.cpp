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

#include "KV6UndoStack.h"

#include <exception>
#include <iterator>
#include <utility>

#include <Core/Debug.h>

namespace spades {
	namespace gui {

		void KV6UndoStack::Begin(const std::string& label) {
			if (depth == 0) {
				pending = Group();
				pending.label = label;
				pending.before = sink.UndoSnapshotState();
			}
			depth++;
		}

		void KV6UndoStack::End() noexcept {
			if (depth == 0)
				return; // unbalanced End; ignore defensively
			if (--depth > 0)
				return; // still inside an outer group
			// Runs from a destructor, possibly while an exception unwinds, so
			// nothing may escape. The edit itself has happened either way: a step
			// that cannot be kept would leave undo replaying into the wrong
			// document, so the history starts afresh instead.
			try {
				pending.after = sink.UndoSnapshotState();
				bool changed = !pending.records.empty() || pending.before != pending.after;
				if (changed)
					Commit();
				else
					pending = Group();
			} catch (const std::exception& ex) {
				SPLog("Undo history cleared: could not record '%s': %s", pending.label.c_str(),
				      ex.what());
				Clear();
			} catch (...) {
				SPLog("Undo history cleared: could not record '%s'", pending.label.c_str());
				Clear();
			}
		}

		void KV6UndoStack::RecordVoxel(int x, int y, int z, bool oldSolid, uint32_t oldColor,
		                               bool newSolid, uint32_t newColor) {
			if (depth == 0)
				return; // only recorded inside a group
			if (oldSolid == newSolid && oldColor == newColor)
				return; // no-op
			pending.records.push_back(
			  Record::MakeVoxel(x, y, z, oldSolid, oldColor, newSolid, newColor));
			pending.hasGeometry = true;
		}

		void KV6UndoStack::RecordReframe(int beforeW, int beforeH, int beforeD, int afterW,
		                                 int afterH, int afterD, int ox, int oy, int oz) {
			if (depth == 0)
				return;
			pending.records.push_back(
			  Record::MakeReframe(beforeW, beforeH, beforeD, afterW, afterH, afterD, ox, oy, oz));
			pending.hasGeometry = true;
		}

		void KV6UndoStack::RecordOrigin(const Vector3& before, const Vector3& after) {
			if (depth == 0)
				return;
			if (before.x == after.x && before.y == after.y && before.z == after.z)
				return; // no-op
			pending.records.push_back(Record::MakeOrigin(before, after));
			pending.hasGeometry = true; // the pivot is saved to the file -> dirties it
		}

		void KV6UndoStack::BeginAction() {
			action = ++nextAction;
			if (action == 0)
				action = ++nextAction; // 0 means "no action"; skip it on wrap-around
		}

		void KV6UndoStack::Commit() {
			pending.geomBefore = geomId;
			// Only geometry edits advance the geometry id (drives the dirty flag);
			// steps that change only the rest of the edit state share the
			// surrounding geometry state.
			pending.geomAfter = pending.hasGeometry ? ++nextGeomId : geomId;
			geomId = pending.geomAfter;
			pending.action = action;

			for (const Group& g : redoGroups)
				totalBytes -= g.bytes;
			redoGroups.clear(); // a fresh edit invalidates the redo branch

			if (action != 0 && !undoGroups.empty() && undoGroups.back().action == action) {
				// Later in the same user action: extend its step, which keeps the
				// label and the "before" state of the action's first edit.
				Group& step = undoGroups.back();
				totalBytes -= step.bytes;
				step.records.insert(step.records.end(),
				                    std::make_move_iterator(pending.records.begin()),
				                    std::make_move_iterator(pending.records.end()));
				step.after = std::move(pending.after);
				step.hasGeometry = step.hasGeometry || pending.hasGeometry;
				step.geomAfter = pending.geomAfter;
				// An action that put everything back (select, then deselect) leaves
				// nothing to undo, so it leaves no step either.
				if (step.records.empty() && step.before == step.after) {
					undoGroups.pop_back();
				} else {
					step.bytes = BytesOf(step);
					totalBytes += step.bytes;
				}
			} else {
				pending.bytes = BytesOf(pending);
				totalBytes += pending.bytes;
				undoGroups.push_back(std::move(pending));
			}
			pending = Group();

			// Evict the oldest history past either cap, but never the last step.
			while ((undoGroups.size() > kMaxGroups || totalBytes > kMaxBytes) &&
			       undoGroups.size() > 1) {
				totalBytes -= undoGroups.front().bytes;
				undoGroups.pop_front();
			}
		}

		std::size_t KV6UndoStack::BytesOf(const Group& g) {
			return g.records.size() * sizeof(Record) + g.after.UnsharedBytes(g.before);
		}

		// Replay a group's records forward (redo): frames then their dependent voxels,
		// in recorded order.
		void KV6UndoStack::ApplyForward(const Group& g) {
			for (const Record& r : g.records) {
				if (r.kind == Record::Kind::Voxel)
					sink.UndoApplyVoxel(r.v.x, r.v.y, r.v.z, r.v.newSolid, r.v.newColor);
				else if (r.kind == Record::Kind::Reframe)
					sink.UndoApplyReframe(r.r.aw, r.r.ah, r.r.ad, r.r.ox, r.r.oy, r.r.oz);
				else
					sink.UndoApplyOrigin(MakeVector3(r.o.ax, r.o.ay, r.o.az));
			}
			sink.UndoRestoreState(g.after);
			sink.UndoReplayed();
		}

		// Replay a group's records in reverse, each inverted (undo). Processing in
		// reverse means a reframe is undone after the voxels recorded after it, so the
		// frame those later voxels live in is still valid when they are reverted.
		void KV6UndoStack::ApplyInverse(const Group& g) {
			for (auto it = g.records.rbegin(); it != g.records.rend(); ++it) {
				const Record& r = *it;
				if (r.kind == Record::Kind::Voxel)
					sink.UndoApplyVoxel(r.v.x, r.v.y, r.v.z, r.v.oldSolid, r.v.oldColor);
				else if (r.kind == Record::Kind::Reframe) // back to before-dims, negated offset
					sink.UndoApplyReframe(r.r.bw, r.r.bh, r.r.bd, -r.r.ox, -r.r.oy, -r.r.oz);
				else
					sink.UndoApplyOrigin(MakeVector3(r.o.bx, r.o.by, r.o.bz));
			}
			sink.UndoRestoreState(g.before);
			sink.UndoReplayed();
		}

		bool KV6UndoStack::Undo() {
			if (depth != 0 || undoGroups.empty())
				return false;
			action = 0; // nothing done after this merges into what came before
			Group g = std::move(undoGroups.back());
			undoGroups.pop_back();
			ApplyInverse(g);
			geomId = g.geomBefore;
			redoGroups.push_back(std::move(g));
			return true;
		}

		bool KV6UndoStack::Redo() {
			if (depth != 0 || redoGroups.empty())
				return false;
			action = 0;
			Group g = std::move(redoGroups.back());
			redoGroups.pop_back();
			ApplyForward(g);
			geomId = g.geomAfter;
			undoGroups.push_back(std::move(g));
			return true;
		}

		void KV6UndoStack::Clear() noexcept {
			undoGroups.clear();
			redoGroups.clear();
			pending = Group();
			depth = 0;
			action = 0;
			// Ids are never reused: the geometry kept its unrecorded changes (when a
			// step could not be recorded), so it must not match an id it was saved at.
			geomId = ++nextGeomId;
			totalBytes = 0;
		}

	} // namespace gui
} // namespace spades
