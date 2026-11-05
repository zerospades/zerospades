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

#include "ContainerTool.h"
#include "SelectSubTools.h"

namespace spades {
	namespace gui {
		// Select voxels: Voxel (single), Box (box region), By Colour, Cylinder,
		// Plane (a flat face), and on the screen Rect and Brush. Its bar holds
		// the commands that act on the selection, clipboard included, and the
		// screen selections' settings; the colour picker recolours what is
		// selected.
		class SelectTool : public ContainerTool {
		public:
			SelectTool();
			const char* Label() const override { return "Select"; }

			ToolOptions* Options() override;
			// Commands with nothing to act on are greyed out.
			void UpdateOptions(IVoxelEditContext& ed) override;
			void OnAction(IVoxelEditContext& ed, const std::string& id) override;
			void OnOptionToggled(IVoxelEditContext& ed, const std::string& id, bool value) override;
			void OnOptionNumberChanged(IVoxelEditContext& ed, const std::string& id, float value,
			                           bool committed) override;
			// A colour chosen on the picker recolours the selection, live while
			// it is being chosen.
			void OnBrushColorChanged(IVoxelEditContext& ed, std::uint32_t color,
			                         bool committed) override;
			std::string Hint(IVoxelEditContext& ed) override;

		private:
			ToolOptions options;
			ScreenSelectSettings screen; // Rect's and Brush's
			int rectIndex = -1, brushIndex = -1;
		};
	} // namespace gui
} // namespace spades
