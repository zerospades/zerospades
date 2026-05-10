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

#include <memory>
#include <vector>

#include "KV6EditorTool.h"
#include "KV6SubTool.h"

namespace spades {
	namespace gui {
		// A main tool that is just a set of sub-tools (shown in the secondary
		// toolbar). All input/drawing forwards to the active sub-tool, so concrete
		// tools (Draw, Select) only have to populate `subs` and provide a Label.
		class MultiSubTool : public EditorTool {
		public:
			int SubToolCount() const override { return int(subs.size()); }
			const char* SubToolLabel(int i) const override { return subs[i]->Label(); }
			int ActiveSubTool() const override { return active; }
			void SetSubTool(KV6EditorView&, int) override;

			void OnActivate(KV6EditorView&) override;
			void OnPointerDown(KV6EditorView&, const std::string& button) override;
			void OnPointerUp(KV6EditorView&, const std::string& button) override;
			void OnKey(KV6EditorView&, const std::string& key, bool down) override;
			void DrawScene(KV6EditorView&) override;

		protected:
			std::vector<std::unique_ptr<SubTool>> subs;
			int active = 0;
			SubTool* Cur();
		};
	} // namespace gui
} // namespace spades
