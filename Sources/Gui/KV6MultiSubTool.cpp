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

#include "KV6MultiSubTool.h"

namespace spades {
	namespace gui {
		SubTool* MultiSubTool::Cur() {
			return (active >= 0 && active < int(subs.size())) ? subs[active].get() : nullptr;
		}

		void MultiSubTool::SetSubTool(KV6EditorView& ed, int i) {
			if (i == active || i < 0 || i >= int(subs.size()))
				return;
			active = i;
			if (SubTool* s = Cur())
				s->OnActivate(ed);
		}

		void MultiSubTool::OnActivate(KV6EditorView& ed) {
			if (SubTool* s = Cur())
				s->OnActivate(ed);
		}
		void MultiSubTool::OnPointerDown(KV6EditorView& ed, const std::string& button) {
			if (SubTool* s = Cur())
				s->OnPointerDown(ed, button);
		}
		void MultiSubTool::OnPointerUp(KV6EditorView& ed, const std::string& button) {
			if (SubTool* s = Cur())
				s->OnPointerUp(ed, button);
		}
		void MultiSubTool::OnKey(KV6EditorView& ed, const std::string& key, bool down) {
			if (SubTool* s = Cur())
				s->OnKey(ed, key, down);
		}
		void MultiSubTool::DrawScene(KV6EditorView& ed) {
			if (SubTool* s = Cur())
				s->DrawScene(ed);
		}
	} // namespace gui
} // namespace spades
