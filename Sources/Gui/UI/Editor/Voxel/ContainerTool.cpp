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

#include "ContainerTool.h"

namespace spades {
	namespace gui {
		const VoxelTool* ContainerTool::Cur() const {
			return (active >= 0 && active < int(subs.size())) ? subs[active].get() : nullptr;
		}

		VoxelTool* ContainerTool::Cur() {
			return const_cast<VoxelTool*>(const_cast<const ContainerTool*>(this)->Cur());
		}

		ToolOptions* ContainerTool::Options() {
			// Built here rather than kept in step: every value, enabled state and
			// label on it comes from the tools each frame, and this runs after
			// all of that has happened (the editor updates, then draws).
			shown.Clear();
			shown.Append(options);
			if (VoxelTool* s = Cur()) {
				if (ToolOptions* theirs = s->Options())
					shown.Append(*theirs);
			}
			return &shown;
		}

		void ContainerTool::UpdateOptions(IVoxelEditContext& ed) {
			if (VoxelTool* s = Cur())
				s->UpdateOptions(ed);
		}

		// Anything the container did not declare belongs to the sub-tool whose
		// options it came from, which is the active one: the bar only ever shows
		// that one's.
		void ContainerTool::OnAction(IVoxelEditContext& ed, const std::string& id) {
			if (Owns(id))
				return;
			if (VoxelTool* s = Cur())
				s->OnAction(ed, id);
		}

		void ContainerTool::OnOptionToggled(IVoxelEditContext& ed, const std::string& id,
		                                    bool value) {
			if (Owns(id))
				return;
			if (VoxelTool* s = Cur())
				s->OnOptionToggled(ed, id, value);
		}

		void ContainerTool::OnOptionNumberChanged(IVoxelEditContext& ed, const std::string& id,
		                                          float value, bool committed) {
			if (Owns(id))
				return;
			if (VoxelTool* s = Cur())
				s->OnOptionNumberChanged(ed, id, value, committed);
		}

		void ContainerTool::SetSubTool(IVoxelEditContext& ed, int i) {
			if (i == active || i < 0 || i >= int(subs.size()))
				return;
			// The outgoing sub-tool finishes what it had in progress first.
			if (VoxelTool* s = Cur())
				s->OnDeactivate(ed);
			active = i;
			if (VoxelTool* s = Cur())
				s->OnActivate(ed);
		}

		void ContainerTool::OnActivate(IVoxelEditContext& ed) {
			if (VoxelTool* s = Cur())
				s->OnActivate(ed);
		}
		void ContainerTool::OnDeactivate(IVoxelEditContext& ed) {
			if (VoxelTool* s = Cur())
				s->OnDeactivate(ed);
		}
		void ContainerTool::OnPointer(IVoxelEditContext& ed, const PointerInput& e) {
			if (VoxelTool* s = Cur())
				s->OnPointer(ed, e);
		}
		void ContainerTool::OnKey(IVoxelEditContext& ed, const KeyInput& e) {
			if (VoxelTool* s = Cur())
				s->OnKey(ed, e);
		}
		std::string ContainerTool::EscapeLabel(IVoxelEditContext& ed) {
			VoxelTool* s = Cur();
			return s ? s->EscapeLabel(ed) : std::string();
		}
		void ContainerTool::OnEscape(IVoxelEditContext& ed) {
			if (VoxelTool* s = Cur())
				s->OnEscape(ed);
		}
		void ContainerTool::CancelInteraction(IVoxelEditContext& ed) {
			if (VoxelTool* s = Cur())
				s->CancelInteraction(ed);
		}
		void ContainerTool::OnDocumentChanged(IVoxelEditContext& ed) {
			if (VoxelTool* s = Cur())
				s->OnDocumentChanged(ed);
		}
		std::string ContainerTool::Hint(IVoxelEditContext& ed) {
			VoxelTool* s = Cur();
			return s ? s->Hint(ed) : std::string();
		}
		void ContainerTool::DrawScene(IVoxelEditContext& ed) {
			if (VoxelTool* s = Cur())
				s->DrawScene(ed);
		}
		void ContainerTool::DrawOverlay(IVoxelEditContext& ed) {
			if (VoxelTool* s = Cur())
				s->DrawOverlay(ed);
		}
	} // namespace gui
} // namespace spades
