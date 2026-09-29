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

#include "VoxelTool.h"
#include "SubTool.h"

namespace spades {
	namespace gui {
		// A tool that is just a set of child tools (shown in the secondary toolbar).
		// All input/drawing forwards to the active child, so concrete tools (Draw,
		// Select) only have to populate `subs` and provide a Label.
		class ContainerTool : public VoxelTool {
		public:
			int SubToolCount() const override { return int(subs.size()); }
			const char* SubToolLabel(int i) const override { return subs[i]->Label(); }
			VoxelTool* SubTool(int i) override {
				return (i >= 0 && i < int(subs.size())) ? subs[i].get() : nullptr;
			}
			int ActiveSubTool() const override { return active; }
			void SetSubTool(IVoxelEditContext&, int) override;

			void OnActivate(IVoxelEditContext&) override;
			void OnDeactivate(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			void OnKey(IVoxelEditContext&, const KeyInput&) override;
			std::string EscapeLabel(IVoxelEditContext&) override;
			void OnEscape(IVoxelEditContext&) override;
			void CancelInteraction(IVoxelEditContext&) override;
			void OnDocumentChanged(IVoxelEditContext&) override;
			std::string Hint(IVoxelEditContext&) override;
			void DrawScene(IVoxelEditContext&) override;
			void DrawOverlay(IVoxelEditContext&) override;

		protected:
			std::vector<std::unique_ptr<VoxelTool>> subs;
			int active = 0;
			VoxelTool* Cur();
		};
	} // namespace gui
} // namespace spades
