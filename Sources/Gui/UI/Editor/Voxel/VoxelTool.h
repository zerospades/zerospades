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

#include <cstdint>

#include <Gui/UI/Editor/Shell/EditorTool.h>
#include <Gui/UI/Editor/Shell/ToolRegistry.h>

namespace spades {
	namespace gui {
		class IVoxelEditContext;

		/** A tool of a voxel editor: one working on cells through IVoxelEditContext. */
		class VoxelTool : public BasicEditorTool<IVoxelEditContext> {
		public:
			// Whether the right button does the inverse of the left in this
			// (top-level) tool, as erase is of fill and deselect of select. A tool
			// whose action has no inverse (recolouring) says no, and the editor
			// then keeps the right button from all of its sub-tools.
			virtual bool RightButtonInverts() const { return true; }

			// The user changed the brush colour on the colour picker. `committed`
			// is false while the picker is still being dragged -- show it, but
			// record nothing undo will have to unpick -- and true once the press
			// is over.
			virtual void OnBrushColorChanged(IVoxelEditContext&, std::uint32_t color,
			                                 bool committed) {
				(void)color;
				(void)committed;
			}
		};

		using VoxelToolSlot = BasicToolSlot<VoxelTool>;
		using VoxelToolRegistry = BasicToolRegistry<VoxelTool>;

		/** The voxel editors' tools, seeded with the built-in ones on first use. */
		VoxelToolRegistry& VoxelTools();
	} // namespace gui
} // namespace spades
