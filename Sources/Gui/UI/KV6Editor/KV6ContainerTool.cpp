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

#include "KV6ContainerTool.h"

namespace spades {
	namespace gui {
		EditorTool* ContainerTool::Cur() {
			return (active >= 0 && active < int(subs.size())) ? subs[active].get() : nullptr;
		}

		void ContainerTool::SetSubTool(IEditorContext& ed, int i) {
			if (i == active || i < 0 || i >= int(subs.size()))
				return;
			active = i;
			if (EditorTool* s = Cur())
				s->OnActivate(ed);
		}

		void ContainerTool::OnActivate(IEditorContext& ed) {
			if (EditorTool* s = Cur())
				s->OnActivate(ed);
		}
		void ContainerTool::OnPointer(IEditorContext& ed, const PointerInput& e) {
			if (EditorTool* s = Cur())
				s->OnPointer(ed, e);
		}
		void ContainerTool::OnKey(IEditorContext& ed, const KeyInput& e) {
			if (EditorTool* s = Cur())
				s->OnKey(ed, e);
		}
		bool ContainerTool::OnEscape(IEditorContext& ed) {
			EditorTool* s = Cur();
			return s ? s->OnEscape(ed) : false;
		}
		void ContainerTool::DrawScene(IEditorContext& ed) {
			if (EditorTool* s = Cur())
				s->DrawScene(ed);
		}
		void ContainerTool::DrawOverlay(IEditorContext& ed) {
			if (EditorTool* s = Cur())
				s->DrawOverlay(ed);
		}
	} // namespace gui
} // namespace spades
