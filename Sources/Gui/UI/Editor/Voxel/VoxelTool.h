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

#include <Gui/UI/Editor/Shell/EditorTool.h>
#include <Gui/UI/Editor/Shell/ToolRegistry.h>

namespace spades {
	namespace gui {
		class IVoxelEditContext;

		// What a top-level tool does with cells, so sub-tools (incl. scripted ones)
		// can apply through IVoxelEditContext::ApplyCells without knowing their
		// host. The right button does the inverse of the left (erase, deselect);
		// Paint has no inverse, so the editor keeps the right button from its
		// sub-tools.
		enum class EditorRole { Edit, Select, Paint };

		/** A tool of a voxel editor: one working on cells through IVoxelEditContext. */
		class VoxelTool : public BasicEditorTool<IVoxelEditContext> {
		public:
			// Whether this (top-level) tool edits voxels or builds a selection.
			// `ApplyCells` routes by the active tool's role.
			virtual EditorRole Role() const { return EditorRole::Edit; }
		};

		using VoxelToolSlot = BasicToolSlot<VoxelTool>;
		using VoxelToolRegistry = BasicToolRegistry<VoxelTool>;

		/** The voxel editors' tools, seeded with the built-in ones on first use. */
		VoxelToolRegistry& VoxelTools();
	} // namespace gui
} // namespace spades
