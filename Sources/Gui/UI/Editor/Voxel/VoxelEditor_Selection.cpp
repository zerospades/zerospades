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
// VoxelEditor: the selection.

#include "VoxelEditorInternal.h"

namespace spades {
	namespace gui {
		// --- Selection --------------------------------------------------------

		bool VoxelEditor::IsSelected(int x, int y, int z) const {
			return edit.selection.Contains(MakeIntVector3(x, y, z));
		}

		bool VoxelEditor::SelectionBounds(IntVector3& lo, IntVector3& hi) const {
			if (edit.placing) {
				lo = edit.placement.anchor;
				hi = lo + edit.placement.Extent() - MakeIntVector3(1, 1, 1);
				return true;
			}
			return edit.selection.Bounds(lo, hi);
		}

		int VoxelEditor::SelectionCount() const {
			// While a paste or an import waits it is what the selection commands
			// act on.
			return edit.placing ? int(edit.placement.voxels->size()) : edit.selection.Size();
		}

		void VoxelEditor::ClearSelection() {
			const int count = SelectionCount();
			{
				DocumentCommand command(*this);
				VoxelUndoStack::Step step(undo, "Clear Selection");
				edit.selection.Clear();
			}
			// Reported here rather than by whoever asked: a command that seems to
			// do nothing reads as broken, and it is asked for from the menu, a
			// key and a button alike.
			SetStatus(count ? "Deselected " + std::to_string(count) + " voxels"
			                : "Nothing was selected");
		}

		std::vector<IntVector3> VoxelEditor::LinkedColorRegion(int x, int y, int z) const {
			std::vector<IntVector3> region;
			if (!document.IsSolid(x, y, z))
				return region;
			const std::uint32_t target = document.Color(x, y, z);
			auto linked = [&](const IntVector3& v) {
				return document.IsSolid(v.x, v.y, v.z) && document.Color(v.x, v.y, v.z) == target;
			};
			// Every cell is queued once: `queued` holds all that ever were.
			VoxelSelection queued;
			std::vector<IntVector3> stack(1, MakeIntVector3(x, y, z));
			queued.Add(stack.back());
			while (!stack.empty()) {
				const IntVector3 c = stack.back();
				stack.pop_back();
				region.push_back(c);
				for (const IntVector3& step : kFaceNeighbours) {
					const IntVector3 n = c + step;
					if (!queued.Contains(n) && linked(n)) {
						queued.Add(n);
						stack.push_back(n);
					}
				}
			}
			return region;
		}

		std::vector<IntVector3> VoxelEditor::CoplanarFaces(const IntVector3& solid,
		                                                   const IntVector3& normal) const {
			std::vector<IntVector3> region;
			auto showing = [&](const IntVector3& v) {
				const IntVector3 out = v + normal;
				return document.IsSolid(v.x, v.y, v.z) && !document.IsSolid(out.x, out.y, out.z);
			};
			if (!showing(solid))
				return region;
			// Across the plane only: the four face steps square to the normal.
			std::vector<IntVector3> steps;
			for (const IntVector3& step : kFaceNeighbours) {
				if (step.x * normal.x + step.y * normal.y + step.z * normal.z == 0)
					steps.push_back(step);
			}
			VoxelSelection queued;
			std::vector<IntVector3> stack(1, solid);
			queued.Add(solid);
			while (!stack.empty()) {
				const IntVector3 c = stack.back();
				stack.pop_back();
				region.push_back(c);
				for (const IntVector3& step : steps) {
					const IntVector3 n = c + step;
					if (!queued.Contains(n) && showing(n)) {
						queued.Add(n);
						stack.push_back(n);
					}
				}
			}
			return region;
		}

		void VoxelEditor::IndexCells() const {
			if (cellsVersion == document.Version())
				return;
			cellsVersion = document.Version();
			solidCells.clear();
			surfaceCells.clear();
			const IntVector3 size = document.Size();
			for (int x = 0; x < size.x; x++)
				for (int y = 0; y < size.y; y++)
					for (int z = 0; z < size.z; z++) {
						if (!document.IsSolid(x, y, z))
							continue;
						const IntVector3 v = MakeIntVector3(x, y, z);
						solidCells.push_back(v);
						for (const IntVector3& step : kFaceNeighbours) {
							const IntVector3 n = v + step;
							if (!document.IsSolid(n.x, n.y, n.z)) {
								surfaceCells.push_back(v);
								break;
							}
						}
					}
		}

		bool VoxelEditor::Seen(const IntVector3& v, const GizmoView& view) const {
			// Through the middle of each face that shows and faces the eye, just
			// inside it, so the ray meets this voxel unless another is in the way.
			const Vector3 middle = MakeVector3(float(v.x), float(v.y), float(v.z));
			for (const IntVector3& step : kFaceNeighbours) {
				const IntVector3 n = v + step;
				if (document.IsSolid(n.x, n.y, n.z))
					continue; // covered
				const Vector3 out = MakeVector3(float(step.x), float(step.y), float(step.z));
				const Vector3 face = middle + out * 0.49F;
				if (Vector3::Dot(out, view.eye - face) <= 0.0F)
					continue; // turned away
				Vector3 dir = face - view.eye;
				const float length = dir.GetLength();
				if (length < 1.0e-4F)
					continue;
				IntVector3 hit, before;
				if (FirstSolidAlong(view.eye, dir / length, hit, before) && hit == v)
					return true;
			}
			return false;
		}

		std::vector<IntVector3> VoxelEditor::CellsOnScreen(
		  const std::function<bool(const Vector2&)>& inside, bool through) const {
			std::vector<IntVector3> found;
			const GizmoView view = DocumentView();
			if (!view.IsValid())
				return found;
			IndexCells();
			// Through the volume, every voxel; else only those with a face
			// showing can be seen, and of them, those that are.
			for (const IntVector3& v : through ? solidCells : surfaceCells) {
				Vector2 screen;
				if (!view.Project(MakeVector3(float(v.x), float(v.y), float(v.z)), screen) ||
				    !inside(screen))
					continue;
				if (through || Seen(v, view))
					found.push_back(v);
			}
			return found;
		}

		void VoxelEditor::SelectIfSolid(const IntVector3& v) {
			if (Editable(v.x, v.y, v.z) && document.IsSolid(v.x, v.y, v.z))
				edit.selection.Add(v);
		}

		void VoxelEditor::SelectBox(const IntVector3& lo, const IntVector3& hi) {
			DocumentCommand command(*this);
			VoxelUndoStack::Step step(undo, "Select");
			for (int x = lo.x; x <= hi.x; x++)
				for (int y = lo.y; y <= hi.y; y++)
					for (int z = lo.z; z <= hi.z; z++)
						SelectIfSolid(MakeIntVector3(x, y, z));
		}

		void VoxelEditor::SelectAll() {
			SelectBox(MakeIntVector3(0, 0, 0), document.Size() - MakeIntVector3(1, 1, 1));
			SetStatus("Selected " + std::to_string(SelectionCount()) + " voxels");
		}

		void VoxelEditor::InvertSelection() {
			{
				DocumentCommand command(*this);
				VoxelUndoStack::Step step(undo, "Invert Selection");
				// Built aside: what is selected now is what is being read.
				const VoxelSelection before = edit.selection;
				edit.selection.Clear();
				const IntVector3 size = document.Size();
				for (int x = 0; x < size.x; x++)
					for (int y = 0; y < size.y; y++)
						for (int z = 0; z < size.z; z++) {
							const IntVector3 v = MakeIntVector3(x, y, z);
							if (!before.Contains(v))
								SelectIfSolid(v);
						}
			}
			SetStatus("Selected " + std::to_string(SelectionCount()) + " voxels");
		}

		void VoxelEditor::SelectCells(const std::vector<IntVector3>& cells) {
			DocumentCommand command(*this);
			VoxelUndoStack::Step step(undo, "Select");
			for (const IntVector3& c : cells)
				SelectIfSolid(c);
		}

		void VoxelEditor::DeselectCells(const std::vector<IntVector3>& cells) {
			DocumentCommand command(*this);
			VoxelUndoStack::Step step(undo, "Deselect");
			for (const IntVector3& c : cells)
				edit.selection.Remove(c);
		}
	} // namespace gui
} // namespace spades
