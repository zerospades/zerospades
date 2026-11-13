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
// VoxelEditor: the edits of the model's voxels, among them hollowing and sweeping, and the brush colour.

#include "VoxelEditorInternal.h"

namespace spades {
	namespace gui {
		// --- Editing ----------------------------------------------------------

		int VoxelEditor::MirrorIdx(int i, float plane) const { return HalfSteps(plane) - i; }

		void VoxelEditor::ExpandMirrors(std::vector<IntVector3>& cells) const {
			const MirrorSetup& mirror = edit.mirror;
			const bool mx = MirrorEnabled(0), my = MirrorEnabled(1), mz = MirrorEnabled(2);
			if (mx || my || mz) {
				std::vector<IntVector3> out;
				out.reserve(cells.size() * 8);
				for (const IntVector3& c : cells) {
					int xs[2] = {c.x, c.x}, nx = 1;
					int ys[2] = {c.y, c.y}, ny = 1;
					int zs[2] = {c.z, c.z}, nz = 1;
					if (mx) {
						int m = MirrorIdx(c.x, mirror.plane.x);
						if (m != c.x) {
							xs[1] = m;
							nx = 2;
						}
					}
					if (my) {
						int m = MirrorIdx(c.y, mirror.plane.y);
						if (m != c.y) {
							ys[1] = m;
							ny = 2;
						}
					}
					if (mz) {
						int m = MirrorIdx(c.z, mirror.plane.z);
						if (m != c.z) {
							zs[1] = m;
							nz = 2;
						}
					}
					for (int a = 0; a < nx; a++)
						for (int b = 0; b < ny; b++)
							for (int d = 0; d < nz; d++)
								out.push_back(MakeIntVector3(xs[a], ys[b], zs[d]));
				}
				cells.swap(out);
			}
			// A cell and its image can both be among the cells (a box across a
			// plane): each is listed once, so counts of what an edit touches hold.
			auto before = [](const IntVector3& a, const IntVector3& b) {
				return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
			};
			std::sort(cells.begin(), cells.end(), before);
			cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
		}

		void VoxelEditor::PlaceCube() {
			DocumentCommand command(*this); // pending voxels land first, then the pick sees them
			DoPick();
			if (pickHit)
				Fill(std::vector<IntVector3>(1, PickPlace()), currentColor, "Place");
		}

		void VoxelEditor::DeleteCube() {
			DocumentCommand command(*this);
			DoPick();
			if (pickHit)
				Erase(std::vector<IntVector3>(1, PickSolid()), "Delete");
		}

		void VoxelEditor::Fill(std::vector<IntVector3> cells, std::uint32_t color,
		                       const std::string& label) {
			DocumentCommand command(*this);
			ExpandMirrors(cells); // also fill the mirror images, if enabled
			IntVector3 lo, hi;
			if (!BoundsOf(cells, lo, hi))
				return;
			// The cells may lie past the volume: grow it to hold them all, where
			// it can grow.
			const VolumeFrame frame = FrameHolding(lo, hi);
			if (!Fits(frame)) {
				SetStatus(SizeLimitMessage(frame.size));
				return;
			}
			VoxelUndoStack::Step step(undo, label);
			Reframe(frame);
			bool wrote = false;
			for (const IntVector3& c : cells) {
				const IntVector3 at = c + frame.shift;
				if (Editable(at.x, at.y, at.z) && !document.IsSolid(at.x, at.y, at.z)) {
					WriteVoxel(at.x, at.y, at.z, true, color);
					wrote = true;
				}
			}
			if (wrote)
				NoteColorUsed(color);
		}

		void VoxelEditor::Erase(std::vector<IntVector3> cells, const std::string& label) {
			DocumentCommand command(*this);
			ExpandMirrors(cells); // also erase the mirror images, if enabled
			int count = 0;
			for (const IntVector3& c : cells) {
				if (Editable(c.x, c.y, c.z) && document.IsSolid(c.x, c.y, c.z))
					count++;
			}
			if (count == 0)
				return;
			if (count > document.RemovableVoxels()) {
				SetStatus(document.RemovalLimitMessage());
				return;
			}
			VoxelUndoStack::Step step(undo, label);
			for (const IntVector3& c : cells) {
				if (Editable(c.x, c.y, c.z) && document.IsSolid(c.x, c.y, c.z))
					WriteVoxel(c.x, c.y, c.z, false, 0);
			}
			TrimVolume();
		}

		// --- Cavity ------------------------------------------------------------

		std::size_t VoxelEditor::Cell(const IntVector3& v) const {
			const IntVector3 size = document.Size();
			return std::size_t(v.x) +
			       std::size_t(size.x) * (std::size_t(v.y) + std::size_t(size.y) * std::size_t(v.z));
		}

		IntVector3 VoxelEditor::CellAt(std::size_t index) const {
			const IntVector3 size = document.Size();
			const std::size_t plane = std::size_t(size.x) * std::size_t(size.y);
			return MakeIntVector3(int(index % std::size_t(size.x)),
			                      int((index % plane) / std::size_t(size.x)), int(index / plane));
		}

		bool VoxelEditor::InVolume(const IntVector3& v) const {
			return InBounds(v.x, v.y, v.z);
		}

		std::vector<bool> VoxelEditor::OutsideAir() const {
			const IntVector3 size = document.Size();
			// A bit a cell: these maps cover the whole volume, and a model may
			// be large across even where it is shallow.
			std::vector<bool> outside(
			  std::size_t(size.x) * std::size_t(size.y) * std::size_t(size.z), false);
			std::vector<std::uint32_t> queue; // cells to flood on from, by index
			// Air at the volume's sides is outside air: past them is open space.
			auto reach = [&](const IntVector3& v) {
				if (!InVolume(v) || document.IsSolid(v.x, v.y, v.z) || outside[Cell(v)])
					return;
				outside[Cell(v)] = true;
				queue.push_back(std::uint32_t(Cell(v)));
			};
			for (int x = 0; x < size.x; x++)
				for (int y = 0; y < size.y; y++) {
					reach(MakeIntVector3(x, y, 0));
					reach(MakeIntVector3(x, y, size.z - 1));
				}
			for (int x = 0; x < size.x; x++)
				for (int z = 0; z < size.z; z++) {
					reach(MakeIntVector3(x, 0, z));
					reach(MakeIntVector3(x, size.y - 1, z));
				}
			for (int y = 0; y < size.y; y++)
				for (int z = 0; z < size.z; z++) {
					reach(MakeIntVector3(0, y, z));
					reach(MakeIntVector3(size.x - 1, y, z));
				}
			// ...and so is whatever air it reaches from there, face by face.
			for (std::size_t i = 0; i < queue.size(); i++) {
				const IntVector3 at = CellAt(queue[i]);
				for (const IntVector3& step : kFaceNeighbours)
					reach(at + step);
			}
			return outside;
		}

		void VoxelEditor::CarveCavity(int thickness) {
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			if (thickness < 1 || edit.selection.Empty()) {
				SetStatus("Nothing selected");
				return;
			}

			const std::vector<bool> outside = OutsideAir();
			auto openAt = [&](const IntVector3& v) {
				// Past the volume is open space, and so is air the outside air
				// reached. Air it did not reach is a cavity already within the
				// model, which this one may join on to.
				return !InVolume(v) || outside[Cell(v)];
			};
			auto onSkin = [&](const IntVector3& v) {
				for (const IntVector3& step : kFaceNeighbours) {
					if (openAt(v + step))
						return true;
				}
				return false;
			};

			// Every selected voxel is the mouth of the cavity, so every one of
			// them has to be a voxel, and one on the outside: a mouth facing
			// nothing but solid or another cavity opens the dig onto nothing.
			int notOnSkin = 0;
			edit.selection.ForEach([&](const IntVector3& v) {
				if (!document.IsSolid(v.x, v.y, v.z) || !onSkin(v))
					notOnSkin++;
			});
			if (notOnSkin > 0) {
				SetStatus("Cavity: " + std::to_string(notOnSkin) + " of the selected voxels " +
				          (notOnSkin == 1 ? "is" : "are") + " not on the model's outside");
				return;
			}

			// How deep each voxel lies under the outside, by faces: the wall is
			// what lies within `thickness` of it, and everything deeper is what
			// a cavity may take.
			const IntVector3 size = document.Size();
			// A byte a cell, being a count rather than a flag; the cells walked
			// are kept as indices, which are a third the size of coordinates.
			std::vector<std::uint8_t> depth(
			  std::size_t(size.x) * std::size_t(size.y) * std::size_t(size.z), 0);
			std::vector<std::uint32_t> queue; // cells to walk on from, by index
			for (int x = 0; x < size.x; x++)
				for (int y = 0; y < size.y; y++)
					for (int z = 0; z < size.z; z++) {
						const IntVector3 v = MakeIntVector3(x, y, z);
						if (!document.IsSolid(x, y, z) || !onSkin(v))
							continue;
						depth[Cell(v)] = 1;
						queue.push_back(std::uint32_t(Cell(v)));
					}
			for (std::size_t i = 0; i < queue.size(); i++) {
				const IntVector3 at = CellAt(queue[i]);
				const std::uint8_t next =
				  std::uint8_t(std::min(254, int(depth[Cell(at)]) + 1)); // deep enough is deep enough
				for (const IntVector3& step : kFaceNeighbours) {
					const IntVector3 n = at + step;
					if (!InVolume(n) || !document.IsSolid(n.x, n.y, n.z) || depth[Cell(n)] != 0)
						continue;
					depth[Cell(n)] = next;
					queue.push_back(std::uint32_t(Cell(n)));
				}
			}
			auto isWall = [&](const IntVector3& v) {
				const std::uint8_t d = depth[Cell(v)];
				return d != 0 && int(d) <= thickness;
			};

			// From the mouth inwards, as far as the solid behind it goes: what
			// the dig can reach is what it takes. It stops at the wall and never
			// steps out into the open, so a cavity stays inside the solid the
			// selection opened and the rest of the model is untouched.
			std::vector<bool> taken(depth.size(), false);
			std::vector<IntVector3> removing;
			queue.clear();
			edit.selection.ForEach([&](const IntVector3& v) {
				taken[Cell(v)] = true;
				removing.push_back(v);
				queue.push_back(std::uint32_t(Cell(v)));
			});
			const std::size_t mouth = removing.size();
			for (std::size_t i = 0; i < queue.size(); i++) {
				const IntVector3 at = CellAt(queue[i]);
				for (const IntVector3& step : kFaceNeighbours) {
					const IntVector3 n = at + step;
					if (!InVolume(n) || taken[Cell(n)] || openAt(n))
						continue;
					taken[Cell(n)] = true;
					if (!document.IsSolid(n.x, n.y, n.z)) {
						queue.push_back(std::uint32_t(Cell(n))); // a cavity already there: the dig joins it
						continue;
					}
					if (isWall(n))
						continue; // the wall is what is left standing
					removing.push_back(n);
					queue.push_back(std::uint32_t(Cell(n)));
				}
			}
			if (removing.size() == mouth) {
				SetStatus("Cavity: nothing behind the selection to hollow out");
				return;
			}

			// The voxels this document will not have edited are left where they
			// are, as every other edit leaves them.
			removing.erase(std::remove_if(removing.begin(), removing.end(),
			                              [&](const IntVector3& v) {
				                              return !Editable(v.x, v.y, v.z);
			                              }),
			               removing.end());
			if (int(removing.size()) > document.RemovableVoxels()) {
				SetStatus(document.RemovalLimitMessage());
				return;
			}

			{
				VoxelUndoStack::Step step(undo, "Cavity");
				edit.selection.Clear(); // the voxels it held are the mouth, now gone
				for (const IntVector3& v : removing)
					WriteVoxel(v.x, v.y, v.z, false, 0);
				TrimVolume();
			}
			SetStatus("Cavity: removed " + std::to_string(removing.size()) + " voxels, wall " +
			          std::to_string(thickness) + (thickness == 1 ? " voxel" : " voxels") + " thick");
		}

		// --- Sweeping (Extrude, Pocket) ----------------------------------------

		std::vector<VoxelEditor::SweptCell> VoxelEditor::SweepPath(int axis, int steps, bool pocket,
		                                                           bool mirrored) const {
			std::vector<SweptCell> path;
			if (axis < 0 || axis > 2 || steps == 0)
				return path;
			IntVector3 unit = MakeIntVector3(0, 0, 0);
			(axis == 0 ? unit.x : axis == 1 ? unit.y : unit.z) = steps > 0 ? 1 : -1;
			const int count = std::abs(steps);
			// An extrusion starts past each voxel; a pocket digs the voxel too.
			const int first = pocket ? 0 : 1;
			const int last = pocket ? count - 1 : count;
			edit.selection.ForEach([&](const IntVector3& v) {
				const std::uint32_t color = document.Color(v.x, v.y, v.z);
				for (int k = first; k <= last; k++) {
					std::vector<IntVector3> cells{v + unit * k};
					if (mirrored)
						ExpandMirrors(cells);
					for (const IntVector3& c : cells)
						path.push_back(SweptCell{c, color});
				}
			});
			// Several voxels sweep through the same cell: it changes once, in
			// the colour of the first to reach it.
			std::stable_sort(path.begin(), path.end(), [](const SweptCell& a, const SweptCell& b) {
				return std::tie(a.at.x, a.at.y, a.at.z) < std::tie(b.at.x, b.at.y, b.at.z);
			});
			path.erase(std::unique(path.begin(), path.end(),
			                       [](const SweptCell& a, const SweptCell& b) { return a.at == b.at; }),
			           path.end());
			return path;
		}

		std::vector<IntVector3> VoxelEditor::SweptCells(int axis, int steps, bool pocket) const {
			std::vector<IntVector3> cells;
			for (const SweptCell& c : SweepPath(axis, steps, pocket, false)) {
				const IntVector3& at = c.at;
				const bool solid = InBounds(at.x, at.y, at.z) && document.IsSolid(at.x, at.y, at.z);
				// A pocket digs what is there; an extrusion fills what is not.
				if (pocket ? (solid && Editable(at.x, at.y, at.z)) : !solid)
					cells.push_back(at);
			}
			return cells;
		}

		void VoxelEditor::SweepSelection(int axis, int steps, bool pocket) {
			if (axis < 0 || axis > 2 || steps == 0)
				return;
			DocumentCommand command(*this);
			if (edit.selection.Empty())
				return;
			IntVector3 reach = MakeIntVector3(0, 0, 0);
			(axis == 0 ? reach.x : axis == 1 ? reach.y : reach.z) = steps;
			std::vector<IntVector3> selected;
			edit.selection.ForEach([&](const IntVector3& v) { selected.push_back(v); });
			const std::vector<SweptCell> path = SweepPath(axis, steps, pocket, true);

			if (pocket) {
				int count = 0;
				for (const SweptCell& c : path) {
					if (Editable(c.at.x, c.at.y, c.at.z) && document.IsSolid(c.at.x, c.at.y, c.at.z))
						count++;
				}
				if (count == 0) {
					SetStatus("Nothing to dig there");
					return;
				}
				if (count > document.RemovableVoxels()) {
					SetStatus(document.RemovalLimitMessage());
					return;
				}
				VoxelUndoStack::Step step(undo, "Pocket");
				for (const SweptCell& c : path) {
					if (Editable(c.at.x, c.at.y, c.at.z) && document.IsSolid(c.at.x, c.at.y, c.at.z))
						WriteVoxel(c.at.x, c.at.y, c.at.z, false, 0);
				}
				// The pockets' floors are selected, so the next sweep digs on.
				edit.selection.Clear();
				for (const IntVector3& v : selected)
					SelectIfSolid(v + reach);
				TrimVolume();
				return;
			}

			std::vector<IntVector3> cells;
			for (const SweptCell& c : path)
				cells.push_back(c.at);
			IntVector3 lo, hi;
			if (!BoundsOf(cells, lo, hi))
				return;
			const VolumeFrame frame = FrameHolding(lo, hi);
			if (!Fits(frame)) {
				SetStatus(SizeLimitMessage(frame.size));
				return;
			}
			VoxelUndoStack::Step step(undo, "Extrude");
			Reframe(frame);
			bool wrote = false;
			for (const SweptCell& c : path) {
				const IntVector3 at = c.at + frame.shift;
				if (Editable(at.x, at.y, at.z) && !document.IsSolid(at.x, at.y, at.z)) {
					WriteVoxel(at.x, at.y, at.z, true, c.color);
					wrote = true;
				}
			}
			if (!wrote)
				SetStatus("Nothing to extrude: the way is filled");
			// The ends of the extrusion are selected, so the next sweep goes on
			// from them.
			edit.selection.Clear();
			for (const IntVector3& v : selected)
				SelectIfSolid(v + reach + frame.shift);
		}

		void VoxelEditor::FillCells(const std::vector<IntVector3>& cells, std::uint32_t color) {
			Fill(cells, color, "Fill");
		}

		void VoxelEditor::EraseCells(const std::vector<IntVector3>& cells) {
			Erase(cells, "Erase");
		}

		void VoxelEditor::PaintCells(const std::vector<IntVector3>& cellsIn, std::uint32_t color) {
			DocumentCommand command(*this);
			std::vector<IntVector3> cells = cellsIn;
			ExpandMirrors(cells); // also recolour the mirror images, if enabled
			std::uint32_t rgb = color & 0xFFFFFF;
			VoxelUndoStack::Step step(undo, "Paint");
			bool wrote = false;
			for (const IntVector3& c : cells) {
				if (!Editable(c.x, c.y, c.z) || !document.IsSolid(c.x, c.y, c.z))
					continue; // paint only existing voxels; never grows the volume
				if (document.Color(c.x, c.y, c.z) != rgb) {
					WriteVoxel(c.x, c.y, c.z, true, rgb);
					wrote = true;
				}
			}
			if (wrote)
				NoteColorUsed(rgb);
		}

		// --- Colour -----------------------------------------------------------

		Vector4 VoxelEditor::ColorToVec(std::uint32_t c) const {
			return ConvertColorRGBA(IntVectorFromColor(c));
		}

		bool VoxelEditor::SamplingArmed() const {
			return altHeld || ui.GetColorPicker()->GetEyedropperMode();
		}

		bool VoxelEditor::SampleColor() {
			DoPick();
			if (!pickHit)
				return false;
			// Into whatever the picker is setting: the brush, or the colour a
			// tool has it set instead.
			const std::uint32_t picked = document.Color(pickSolid.x, pickSolid.y, pickSolid.z);
			if (!SetToolColor(picked))
				currentColor = picked;
			ColorPicker& picker = *ui.GetColorPicker();
			picker.SetColor(picked);
			picker.SetEyedropperMode(false); // a pick is one-shot
			SetStatus("Picked colour");
			return true;
		}

		void VoxelEditor::NoteColorUsed(std::uint32_t color) {
			ui.GetColorPicker()->AddRecentColor(color);
		}

		void VoxelEditor::NoteDocumentColors() {
			// Only voxels with a face open to the air count: those inside are
			// never seen, and a solid interior would outweigh every colour
			// that is.
			const IntVector3 size = document.Size();
			std::unordered_map<std::uint32_t, int> uses;
			for (int x = 0; x < size.x; x++)
				for (int y = 0; y < size.y; y++)
					for (int z = 0; z < size.z; z++) {
						if (!document.IsSolid(x, y, z))
							continue;
						bool exposed = false;
						for (const IntVector3& step : kFaceNeighbours) {
							const IntVector3 n = MakeIntVector3(x, y, z) + step;
							if (!InBounds(n.x, n.y, n.z) || !document.IsSolid(n.x, n.y, n.z)) {
								exposed = true;
								break;
							}
						}
						if (exposed)
							uses[document.Color(x, y, z) & 0xFFFFFF]++;
					}

			std::vector<std::pair<std::uint32_t, int>> ranked(uses.begin(), uses.end());
			const std::size_t kept =
			  std::min(ranked.size(), std::size_t(ColorPicker::kRecentSlots));
			// Most used first; equal counts in colour order, so the row is the
			// same every time the model is opened.
			std::partial_sort(ranked.begin(), ranked.begin() + std::ptrdiff_t(kept), ranked.end(),
			                  [](const std::pair<std::uint32_t, int>& a,
			                     const std::pair<std::uint32_t, int>& b) {
				                  return a.second != b.second ? a.second > b.second
				                                              : a.first < b.first;
			                  });
			// Each colour noted goes first in the row, so the most used goes last.
			for (std::size_t i = kept; i-- > 0;)
				NoteColorUsed(ranked[i].first);
		}
	} // namespace gui
} // namespace spades
