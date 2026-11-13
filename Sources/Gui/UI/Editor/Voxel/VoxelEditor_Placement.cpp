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
// VoxelEditor: the clipboard, and voxels waiting to be placed.

#include "VoxelEditorInternal.h"

namespace spades {
	namespace gui {
		// --- Clipboard / paste -----------------------------------------------
		//
		// Copy, Cut and Delete act on the selection. While a paste or an import
		// waits, it is what the user is handling, so the commands act on it
		// where it is, and never place it first.

		void VoxelEditor::CopySelection() {
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			std::vector<ClipVoxel> copied;
			IntVector3 lo, hi;
			if (edit.placing) {
				copied = *edit.placement.voxels; // already relative to their box
			} else if (edit.selection.Bounds(lo, hi)) {
				copied.reserve(size_t(edit.selection.Size()));
				edit.selection.ForEach([&](const IntVector3& v) {
					copied.push_back({v - lo, document.Color(v.x, v.y, v.z)});
				});
			}
			if (copied.empty()) {
				SetStatus("Nothing selected");
				return;
			}
			clipboard = std::move(copied);
			SetStatus("Copied " + std::to_string(clipboard.size()) + " voxels");
		}

		bool VoxelEditor::CutSelection() {
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			if (!CanEraseSelection())
				return false;
			CopySelection();
			SetStatus("Cut " + std::to_string(EraseSelection("Cut")) + " voxels");
			return true;
		}

		void VoxelEditor::DeleteSelection() {
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			if (!CanEraseSelection())
				return;
			SetStatus("Deleted " + std::to_string(EraseSelection("Delete")) + " voxels");
		}

		void VoxelEditor::RecolorSelection(std::uint32_t color) {
			// Pending voxels are recoloured where they wait, not placed first.
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			if (SelectionCount() == 0) {
				SetStatus("Nothing selected");
				return;
			}
			const std::uint32_t rgb = color & 0xFFFFFF;
			VoxelUndoStack::Step step(undo, "Recolour");
			int recolored = 0;
			if (edit.placing) {
				bool differs = false;
				for (const ClipVoxel& v : *edit.placement.voxels)
					differs = differs || v.color != rgb;
				// Editing the shared list would copy it even for no change.
				if (differs) {
					for (ClipVoxel& v : edit.placement.voxels.Edit()) {
						if (v.color != rgb) {
							v.color = rgb;
							recolored++;
						}
					}
				}
			} else {
				// A copy to walk: the selection is shared until changed, so it costs
				// nothing, and writing voxels never touches what it is walking.
				const VoxelSelection selected = edit.selection;
				selected.ForEach([&](const IntVector3& v) {
					if (!Editable(v.x, v.y, v.z) || !document.IsSolid(v.x, v.y, v.z) ||
					    document.Color(v.x, v.y, v.z) == rgb)
						return;
					WriteVoxel(v.x, v.y, v.z, true, rgb);
					recolored++;
				});
			}
			if (recolored > 0)
				NoteColorUsed(rgb);
			SetStatus("Recoloured " + std::to_string(recolored) + " voxels");
		}

		// Live, non-journaled recolour for a colour still being chosen; the tool
		// commits the final one with RecolorSelection, which puts this back first.
		void VoxelEditor::PreviewRecolorSelection(std::uint32_t color) {
			if (SelectionCount() == 0)
				return;
			const std::uint32_t rgb = color & 0xFFFFFF;
			if (edit.placing) {
				if (!previewedPlacement)
					previewedPlacement = edit.placement; // shares the voxels until edited
				for (ClipVoxel& v : edit.placement.voxels.Edit())
					v.color = rgb;
				return;
			}
			if (!previewedColors) {
				previewedColors.emplace();
				edit.selection.ForEach([&](const IntVector3& v) {
					if (Editable(v.x, v.y, v.z) && document.IsSolid(v.x, v.y, v.z))
						previewedColors->emplace_back(v, document.Color(v.x, v.y, v.z));
				});
			}
			for (const auto& voxel : *previewedColors) {
				const IntVector3& v = voxel.first;
				document.Write(v.x, v.y, v.z, true, rgb);
			}
		}

		bool VoxelEditor::CanEraseSelection() {
			const int count =
			  edit.placing ? int(edit.placement.voxels->size()) : edit.selection.Size();
			if (count == 0) {
				SetStatus("Nothing selected");
				return false;
			}
			// Pending voxels are out of the document already: dropping them
			// leaves what it holds now.
			if (!edit.placing && count > document.RemovableVoxels()) {
				SetStatus(document.RemovalLimitMessage());
				return false;
			}
			return true;
		}

		int VoxelEditor::EraseSelection(const std::string& label) {
			VoxelUndoStack::Step step(undo, label);
			if (edit.placing) {
				// A paste or an import vanishes; the document never held it.
				const int erased = int(edit.placement.voxels->size());
				DropPlacement();
				TrimVolume();
				return erased;
			}
			const VoxelSelection selected = edit.selection;
			edit.selection.Clear();
			selected.ForEach([&](const IntVector3& v) { WriteVoxel(v.x, v.y, v.z, false, 0); });
			TrimVolume();
			return selected.Size();
		}

		void VoxelEditor::Paste() {
			if (clipboard.empty()) {
				SetStatus("Clipboard is empty");
				return;
			}
			// Pending voxels land first, and landing them reframes the volume: the
			// middle is read once the document is the one the paste goes into.
			DocumentCommand command(*this);
			const IntVector3 size = document.Size();
			IntVector3 anchor = MakeIntVector3(size.x / 2, size.y / 2, size.z / 2);
			if (placeAtAim)
				AimedAnchor(ExtentOf(clipboard), anchor);
			StartPlacement(clipboard, "Paste", anchor);
		}

		bool VoxelEditor::AimedAnchor(const IntVector3& extent, IntVector3& anchor) {
			DoPick();
			if (!pickHit)
				return false;
			// Centred across the cell before the hit, the box's near side on it.
			const IntVector3 at = pickPlace;
			anchor = MakeIntVector3(at.x - extent.x / 2, at.y - extent.y / 2, at.z - extent.z / 2);
			const IntVector3 face = pickPlace - pickSolid;
			if (face.x != 0)
				anchor.x = face.x > 0 ? at.x : at.x - extent.x + 1;
			if (face.y != 0)
				anchor.y = face.y > 0 ? at.y : at.y - extent.y + 1;
			if (face.z != 0)
				anchor.z = face.z > 0 ? at.z : at.z - extent.z + 1;
			return true;
		}

		void VoxelEditor::StartPlacement(std::vector<ClipVoxel> voxels, const std::string& label,
		                                 const IntVector3& anchor) {
			if (voxels.empty())
				return;
			// Anything already pending belongs to the document before this starts.
			DocumentCommand command(*this);

			IntVector3 at = anchor;
			if (!ClampPlacementAnchor(ExtentOf(voxels), at)) {
				SetStatus(label + ": too large for the maximum " + document.Noun() + " size");
				return;
			}
			{
				VoxelUndoStack::Step step(undo, label);
				edit.placement = MakePlacement(std::move(voxels), at, label);
				edit.placing = true;
			}

			// Positioning a placement is what the Transform tool is for, so go there.
			if (!ActivatePlacementTool()) {
				SetStatus(label + ": could not open the Transform tool");
				return;
			}
			SetStatus(label + ": position it with the gizmo, then click away to place it");
		}

		bool VoxelEditor::ActivatePlacementTool() {
			const int index = ToolWithRole(ToolRole::Placement);
			if (index < 0)
				return false;
			SetActiveTool(index);
			if (activeTool != index)
				return false;
			// Already there or just arrived, the tool is told what waits: which
			// of its sub-tools positions it is its own to know.
			tools[static_cast<size_t>(index)].tool->OnDocumentChanged(*this);
			return true;
		}

		void VoxelEditor::InsertModel(const VoxelModel& imported) {
			// Pending voxels land first, and landing them moves the pivot the
			// import is aligned on: it is read once that has happened.
			DocumentCommand command(*this);

			// Collect the solid voxels relative to their own min corner. Interior
			// voxels keep the sentinel colour the loader fills them with, exactly as
			// in a document opened from disk; the writer drops them again on save.
			int minX = imported.GetWidth(), minY = imported.GetHeight(),
			    minZ = imported.GetDepth();
			int maxX = -1, maxY = -1, maxZ = -1;
			for (int x = 0; x < imported.GetWidth(); x++)
				for (int y = 0; y < imported.GetHeight(); y++)
					for (int z = 0; z < imported.GetDepth(); z++) {
						if (!imported.IsSolid(x, y, z))
							continue;
						minX = std::min(minX, x);
						maxX = std::max(maxX, x);
						minY = std::min(minY, y);
						maxY = std::max(maxY, y);
						minZ = std::min(minZ, z);
						maxZ = std::max(maxZ, z);
					}
			if (maxX < 0) {
				SetStatus("That model is empty");
				return;
			}

			std::vector<ClipVoxel> voxels;
			for (int x = minX; x <= maxX; x++)
				for (int y = minY; y <= maxY; y++)
					for (int z = minZ; z <= maxZ; z++) {
						if (imported.IsSolid(x, y, z))
							voxels.push_back({MakeIntVector3(x - minX, y - minY, z - minZ),
							                  imported.GetColor(x, y, z) & 0xFFFFFF});
					}

			// Start with the imported pivot on this document's pivot, which puts a
			// part (a hand, a barrel) where it belongs before any dragging.
			Vector3 importedPivot = imported.GetOrigin() * -1.0F;
			Vector3 aligned =
			  MakeVector3(float(minX), float(minY), float(minZ)) + GetPivot() - importedPivot;
			IntVector3 anchor = MakeIntVector3(int(std::floor(aligned.x + 0.5F)),
			                                   int(std::floor(aligned.y + 0.5F)),
			                                   int(std::floor(aligned.z + 0.5F)));
			if (placeAtAim)
				AimedAnchor(ExtentOf(voxels), anchor);
			StartPlacement(std::move(voxels), "Insert", anchor);
		}

		bool VoxelEditor::PlacementFromSelection(PendingPlacement& out) const {
			IntVector3 lo, hi, middle;
			if (!edit.selection.Bounds(lo, hi) || !SelectionMiddleOf(middle))
				return false;
			std::vector<ClipVoxel> voxels;
			voxels.reserve(size_t(edit.selection.Size()));
			edit.selection.ForEach([&](const IntVector3& v) {
				voxels.push_back({v - lo, document.Color(v.x, v.y, v.z)});
			});
			out = MakePlacement(std::move(voxels), lo, "Transform");
			out.pivot = middle;
			return true;
		}

		bool VoxelEditor::SelectionMiddleOf(IntVector3& out) const {
			if (movedMiddle.valid && movedMiddle.selectionVersion == edit.selection.Version() &&
			    !edit.selection.Empty()) {
				out = movedMiddle.at;
				return true;
			}
			IntVector3 lo, hi;
			if (!edit.selection.Bounds(lo, hi))
				return false;
			out = MiddleOf(lo, hi);
			return true;
		}

		void VoxelEditor::MoveSelection(const PlacementTransform& t) {
			PendingPlacement selected;
			if (!PlacementFromSelection(selected))
				return;
			PendingPlacement moved;
			bool clamped = false;
			if (!TransformedPlacement(selected, t, moved, clamped)) {
				SetStatus(SizeLimitMessage(moved.Extent()));
				return;
			}
			if (clamped)
				SetStatus(SizeLimitMessage()); // went as far as the limit allows
			// Where the moved voxels land must fit the volume they leave behind,
			// grown to hold them; with every voxel moving, nothing else holds it.
			const VolumeFrame frame =
			  FrameHolding(moved.anchor, moved.anchor + moved.Extent() - MakeIntVector3(1, 1, 1));
			if (!Fits(frame)) {
				SetStatus(SizeLimitMessage(frame.size));
				return;
			}

			VoxelUndoStack::Step step(undo, t.Turns() ? "Turn" : "Move");
			// They leave first, so a voxel landing where another of them was is
			// written, not wiped out by the one that left.
			const VoxelSelection leaving = edit.selection;
			edit.selection.Clear();
			leaving.ForEach([&](const IntVector3& v) { WriteVoxel(v.x, v.y, v.z, false, 0); });
			LandGroup(moved);
			// The middle stays with the voxels: shifted by the reframe, and by
			// the trim that follows, along with the selection it belongs to.
			movedMiddle.valid = true;
			movedMiddle.selectionVersion = edit.selection.Version();
			movedMiddle.at = moved.pivot + frame.shift;
			TrimVolume();
		}

		int VoxelEditor::LandGroup(const PendingPlacement& group) {
			const VolumeFrame frame =
			  FrameHolding(group.anchor, group.anchor + group.Extent() - MakeIntVector3(1, 1, 1));
			Reframe(frame);
			edit.selection.Clear(); // what lands is what is selected
			int landed = 0;
			for (const ClipVoxel& v : *group.voxels) {
				if (LandVoxel(group.anchor + v.rel + frame.shift, v.color))
					landed++;
			}
			return landed;
		}

		bool VoxelEditor::LandVoxel(const IntVector3& at, std::uint32_t color) {
			if (!Editable(at.x, at.y, at.z))
				return false;
			WriteVoxel(at.x, at.y, at.z, true, color);
			edit.selection.Add(at);
			return true;
		}

		VoxelEditor::DocumentCommand::DocumentCommand(VoxelEditor& editor, Pending pending)
		    : editor(editor) {
			// Counted only once the preparation is done: were it to throw, this
			// scope never existed and later commands must still see depth 0.
			if (editor.documentCommandDepth == 0) {
				editor.EndPreviews();
				if (pending == Pending::Apply)
					editor.ApplyPlacement();
			}
			editor.documentCommandDepth++;
		}

		VoxelEditor::DocumentCommand::~DocumentCommand() {
			if (--editor.documentCommandDepth > 0)
				return;
			// Whatever the command got done, even if it threw part way, the tool
			// catches up with it. Nothing may leave a destructor, so a tool that
			// fails to is logged instead.
			try {
				editor.NotifyDocumentChanged();
			} catch (const std::exception& ex) {
				SPLog("Editor tool failed to follow a document change: %s", ex.what());
			} catch (...) {
				SPLog("Editor tool failed to follow a document change");
			}
		}

		void VoxelEditor::DropPlacement() {
			edit.placing = false;
			edit.placement = PendingPlacement();
		}

		PendingPlacement VoxelEditor::MakePlacement(std::vector<ClipVoxel> voxels,
		                                            const IntVector3& anchor,
		                                            const std::string& label) {
			PendingPlacement p;
			p.voxels = CopyOnWrite<std::vector<ClipVoxel>>(std::move(voxels));
			p.anchor = anchor;
			// A turn about their middle keeps them about where they were.
			p.pivot = MiddleOf(anchor, anchor + p.Extent() - MakeIntVector3(1, 1, 1));
			p.label = label;
			return p;
		}

		bool VoxelEditor::TransformedPlacement(const PendingPlacement& from,
		                                       const PlacementTransform& t, PendingPlacement& out,
		                                       bool& clamped) const {
			out = from;
			clamped = false;

			if (t.Turns()) {
				// A quarter turn takes the axis after `t.axis` onto the one after
				// that, which is the right-handed sense about `t.axis`. Offsets from
				// a whole-voxel centre stay whole, so every voxel lands on a voxel.
				const int turns = ((t.quarterTurns % 4) + 4) % 4;
				const int u = (t.axis + 1) % 3, v = (t.axis + 2) % 3;
				const IntVector3 centre = TurnCentre(from.pivot);
				auto turned = [&](const IntVector3& p) {
					const IntVector3 d = p - centre;
					int c[3] = {d.x, d.y, d.z};
					for (int k = 0; k < turns; k++) {
						const int cu = c[u];
						c[u] = -c[v];
						c[v] = cu;
					}
					return centre + MakeIntVector3(c[0], c[1], c[2]);
				};
				// A turn is the one change that rewrites the voxels themselves.
				std::vector<ClipVoxel>& voxels = out.voxels.Edit();
				std::vector<IntVector3> cells(voxels.size());
				for (size_t i = 0; i < voxels.size(); i++)
					cells[i] = turned(from.anchor + voxels[i].rel);
				IntVector3 lo, hi;
				BoundsOf(cells, lo, hi);
				out.anchor = lo;
				for (size_t i = 0; i < voxels.size(); i++)
					voxels[i].rel = cells[i] - lo;
				// Their middle turns with them, so it is still their middle
				// whichever centre they turned about.
				out.pivot = turned(from.pivot);
			}

			out.anchor = out.anchor + t.shift;
			out.pivot = out.pivot + t.shift;
			IntVector3 fitted = out.anchor;
			if (!ClampPlacementAnchor(out.Extent(), fitted))
				return false;
			clamped = !(fitted == out.anchor);
			// The pivot stays with the voxels, so turning back still returns them.
			out.pivot = out.pivot + (fitted - out.anchor);
			out.anchor = fitted;
			return true;
		}

		void VoxelEditor::TransformPlacement(const PlacementTransform& t) {
			if (!t.IsValid() || t.IsIdentity())
				return;
			if (!edit.placing) {
				// The selection moves in the document itself.
				DocumentCommand command(*this);
				MoveSelection(t);
				return;
			}
			PendingPlacement next;
			bool clamped = false;
			if (!TransformedPlacement(edit.placement, t, next, clamped)) {
				SetStatus(SizeLimitMessage(next.Extent()));
				return;
			}
			if (clamped)
				SetStatus(SizeLimitMessage()); // went as far as the limit allows
			DocumentCommand command(*this, DocumentCommand::Pending::Keep);
			VoxelUndoStack::Step step(undo, t.Turns() ? "Turn" : "Move");
			edit.placement = std::move(next);
		}

		bool VoxelEditor::MovedMiddle(IntVector3& out) const {
			// From what is known already (the waiting voxels' middle, the
			// selection's cached box): it is asked for every frame.
			if (edit.placing) {
				out = edit.placement.pivot;
				return true;
			}
			return SelectionMiddleOf(out);
		}

		bool VoxelEditor::TransformPivot(IntVector3& out) const {
			IntVector3 middle;
			if (!MovedMiddle(middle))
				return false;
			out = TurnCentre(middle);
			return true;
		}

		IntVector3 VoxelEditor::PivotVoxel() const {
			// The voxel nearest the pivot; on a tie the lower one, as for the
			// middle of a group.
			const Vector3 p = GetPivot();
			auto nearest = [](float c) { return int(std::ceil(c - 0.5F)); };
			return MakeIntVector3(nearest(p.x), nearest(p.y), nearest(p.z));
		}

		IntVector3 VoxelEditor::TurnCentre(const IntVector3& groupMiddle) const {
			return turnsAboutModelPivot ? PivotVoxel() : groupMiddle;
		}

		bool VoxelEditor::ClampPlacementAnchor(const IntVector3& box, IntVector3& anchor) const {
			const IntVector3 extentNow = document.Size();
			const IntVector3 limitNow = document.MaxSize();
			const int extent[3] = {box.x, box.y, box.z};
			const int size[3] = {extentNow.x, extentNow.y, extentNow.z};
			const int limit[3] = {limitNow.x, limitNow.y, limitNow.z};
			int at[3] = {anchor.x, anchor.y, anchor.z};
			for (int a = 0; a < 3; a++) {
				if (extent[a] > limit[a] || size[a] > limit[a])
					return false;
				// Landing grows the volume to span min(0, at) .. max(size, at + extent),
				// which stays within the limit exactly for `at` in this range. A
				// volume that cannot grow has its size as its limit, which keeps
				// what lands inside it.
				at[a] = std::max(size[a] - limit[a], std::min(limit[a] - extent[a], at[a]));
			}
			anchor = MakeIntVector3(at[0], at[1], at[2]);
			return true;
		}

		void VoxelEditor::ApplyPlacement() {
			if (!edit.placing)
				return;
			const PendingPlacement pending = edit.placement;

			// Every placement is kept where it fits (ClampPlacementAnchor), so the
			// volume holding it does; were it ever not to, dropping the voxels
			// loses nothing of the document.
			const VolumeFrame frame = FrameHolding(
			  pending.anchor, pending.anchor + pending.Extent() - MakeIntVector3(1, 1, 1));
			if (!Fits(frame)) {
				CancelPlacement();
				SetStatus(SizeLimitMessage(frame.size));
				return;
			}

			int placed = 0;
			{
				VoxelUndoStack::Step step(undo, pending.label);
				// No longer pending before the volume changes, so the relabelling
				// leaves `pending` in the frame it was measured in.
				DropPlacement();
				placed = LandGroup(pending);
				TrimVolume();
			}
			SetStatus(pending.label + ": placed " + std::to_string(placed) + " voxels");
		}

		void VoxelEditor::CancelPlacement() {
			if (!edit.placing)
				return;
			const std::string label = edit.placement.label;
			{
				VoxelUndoStack::Step step(undo, "Cancel " + label);
				DropPlacement();
			}
			SetStatus(label + " cancelled");
		}

		void VoxelEditor::DrawPlacementPreview() {
			// The voxels themselves are rendered solid (see DrawScene); outline the
			// volume so it reads as pending rather than part of the document.
			if (!edit.placing)
				return;
			const PendingPlacement& pending = edit.placement;
			DrawBoxOutline(pending.anchor, pending.anchor + pending.Extent() - MakeIntVector3(1, 1, 1),
			               MakeVector4(0.4F, 1.0F, 0.5F, 0.9F));
		}

		void VoxelEditor::DrawPlacementTransformed(const PlacementTransform& t,
		                                           const Vector4& color) {
			if (!t.IsValid())
				return;
			PendingPlacement selected;
			if (!edit.placing && !PlacementFromSelection(selected))
				return;
			PendingPlacement preview;
			bool clamped = false;
			if (!TransformedPlacement(edit.placing ? edit.placement : selected, t, preview, clamped))
				return;
			for (const ClipVoxel& v : *preview.voxels) {
				const IntVector3 at = preview.anchor + v.rel;
				DrawCellOutline(at.x, at.y, at.z, color);
			}
		}

		void VoxelEditor::RefreshPlacementModel() {
			// The pending voxels' model follows their voxels' version, so any
			// change to them (a turn, an undo, a new paste) rebuilds it and
			// nothing else does.
			const std::uint64_t pendingVersion = edit.placement.voxels.Version();
			if (!edit.placing) {
				placementModel = Handle<client::IModel>();
				placementVoxels = Handle<VoxelModel>();
			} else if (!placementModel || placementModelVersion != pendingVersion) {
				const IntVector3 extent = edit.placement.Extent();
				Handle<VoxelModel> preview = Handle<VoxelModel>::New(extent.x, extent.y, extent.z);
				for (const ClipVoxel& v : *edit.placement.voxels)
					preview->SetSolid(v.rel.x, v.rel.y, v.rel.z, v.color);
				placementModel = renderer->CreateModel(*preview);
				placementVoxels = preview;
				placementModelVersion = pendingVersion;
			}
		}
	} // namespace gui
} // namespace spades
