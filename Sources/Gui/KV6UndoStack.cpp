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

#include <utility>

namespace spades {
	namespace gui {

		void KV6UndoStack::Begin(const std::string& label) {
			if (depth == 0) {
				pending = Group();
				pending.label = label;
				pending.selBefore = sink.UndoSnapshotSelection();
			}
			depth++;
		}

		void KV6UndoStack::End() {
			if (depth == 0)
				return; // unbalanced End; ignore defensively
			if (--depth > 0)
				return; // still inside an outer group
			pending.selAfter = sink.UndoSnapshotSelection();
			bool changed = !pending.records.empty() || pending.selBefore != pending.selAfter;
			if (changed)
				Commit();
			else
				pending = Group();
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

		void KV6UndoStack::Commit() {
			pending.geomBefore = geomId;
			// Only geometry edits advance the geometry id (drives the dirty flag);
			// selection-only steps share the surrounding geometry state.
			pending.geomAfter = pending.hasGeometry ? ++nextGeomId : geomId;
			geomId = pending.geomAfter;

			redoGroups.clear(); // a fresh edit invalidates the redo branch
			undoGroups.push_back(std::move(pending));
			pending = Group();

			while (undoGroups.size() > kMaxGroups)
				undoGroups.pop_front(); // evict the oldest history
		}

		// Replay a group's records forward (redo): frames then their dependent voxels,
		// in recorded order.
		void KV6UndoStack::ApplyForward(const Group& g) {
			for (const Record& r : g.records) {
				if (r.kind == Record::Kind::Voxel)
					sink.UndoApplyVoxel(r.v.x, r.v.y, r.v.z, r.v.newSolid, r.v.newColor);
				else
					sink.UndoApplyReframe(r.r.aw, r.r.ah, r.r.ad, r.r.ox, r.r.oy, r.r.oz);
			}
			sink.UndoRestoreSelection(g.selAfter);
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
				else // inverse reframe: back to the before-dims with the negated offset
					sink.UndoApplyReframe(r.r.bw, r.r.bh, r.r.bd, -r.r.ox, -r.r.oy, -r.r.oz);
			}
			sink.UndoRestoreSelection(g.selBefore);
			sink.UndoReplayed();
		}

		bool KV6UndoStack::Undo() {
			if (depth != 0 || undoGroups.empty())
				return false;
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
			Group g = std::move(redoGroups.back());
			redoGroups.pop_back();
			ApplyForward(g);
			geomId = g.geomAfter;
			undoGroups.push_back(std::move(g));
			return true;
		}

		void KV6UndoStack::Clear() {
			undoGroups.clear();
			redoGroups.clear();
			pending = Group();
			depth = 0;
			geomId = 0;
			nextGeomId = 0;
		}

	} // namespace gui
} // namespace spades
