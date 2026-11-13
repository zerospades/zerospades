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

namespace spades {
	namespace gui {
		// What is done with the voxels selected: Transform moves and turns
		// them, Extrude and Pocket sweep them along an axis. None of them
		// chooses a voxel, which is Select's alone to do; a click on nothing
		// hands the editor back to it. It has no button on the toolbar: it is
		// Select's strip, there while voxels are selected and gone when none
		// are, with the commands on them: Fill, which paints them the brush
		// colour, and Cavity.
		class ModifyTool : public ContainerTool {
		public:
			ModifyTool();
			const char* Label() const override { return "Modify"; }

			// Voxels arriving to be placed are Transform's to position, whichever
			// of the others was at work.
			void OnActivate(IVoxelEditContext& ed) override;
			void OnDocumentChanged(IVoxelEditContext& ed) override;
			// Commands with nothing to act on are greyed out.
			void UpdateOptions(IVoxelEditContext& ed) override;
			void OnAction(IVoxelEditContext& ed, const std::string& id) override;
			// A colour chosen on the picker recolours the selection, as it does
			// while the voxels are being chosen.
			void OnBrushColorChanged(IVoxelEditContext& ed, std::uint32_t color,
			                         bool committed) override;

		private:
			void TakePlacement(IVoxelEditContext& ed);
		};
	} // namespace gui
} // namespace spades
