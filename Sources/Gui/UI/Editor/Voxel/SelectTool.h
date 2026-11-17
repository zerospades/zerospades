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
		class IVoxelSelection;

		// Select voxels: Voxel (single), Box (box region), By Colour, Cylinder,
		// Plane (a flat face), and on the screen Rect and Brush. It only ever
		// changes which voxels are selected -- its bar ends with All, None and
		// Invert -- and what is then done with them is Modify's.
		/** What a colour chosen on the picker does while voxels are selected:
		 *  recolours them, shown as it is chosen and kept once it is. */
		void RecolourSelectionAsChosen(IVoxelSelection& ed, std::uint32_t color, bool committed);

		class SelectTool : public ContainerTool {
		public:
			SelectTool();
			const char* Label() const override { return "Select"; }

			// Commands with nothing to act on are greyed out.
			void UpdateOptions(IVoxelEditContext& ed) override;
			void OnAction(IVoxelEditContext& ed, const std::string& id) override;
			// A colour chosen on the picker recolours the selection, live while
			// it is being chosen.
			void OnBrushColorChanged(IVoxelEditContext& ed, std::uint32_t color,
			                         bool committed) override;
			// A click on nothing selects nothing, whichever way of selecting is
			// active, as in every modeller: but for a box or a cylinder being
			// placed, whose next point may well be in empty space.
			void OnPointer(IVoxelEditContext& ed, const PointerInput& e) override;

		private:
			ScreenSelectSettings screen; // Rect's and Brush's, shared by them
			VoxelsOnScreen voxels;       // what they select
			// The press that may turn out a click on nothing.
			bool pressOnNothing = false;
			Vector2 pressAt = MakeVector2(0.0F, 0.0F);
		};
	} // namespace gui
} // namespace spades
