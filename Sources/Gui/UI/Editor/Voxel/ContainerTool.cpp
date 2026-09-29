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
		VoxelTool* ContainerTool::Cur() {
			return (active >= 0 && active < int(subs.size())) ? subs[active].get() : nullptr;
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
