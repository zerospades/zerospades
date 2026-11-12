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
		class ITransformTarget;

		/** A tool of a voxel editor: one working on cells through IVoxelEditContext. */
		class VoxelTool : public BasicEditorTool<IVoxelEditContext> {
		public:
			// Whether the right button does the inverse of the left in this
			// (top-level) tool, as erase is of fill and deselect of select. A tool
			// whose action has no inverse (recolouring) says no, and the editor
			// then keeps the right button from all of its sub-tools.
			virtual bool RightButtonInverts() const { return true; }
			// Whether the tool has anything to work on. One that has not is
			// greyed out on the toolbar, its keys do nothing, and the editor
			// leaves it for Select should it stop having any while active.
			virtual bool Available(const IVoxelEditContext&) const { return true; }
			// The id of the tool this one is the strip of, for a tool that has
			// no button of its own on the toolbar: its sub-tools are offered
			// on a strip under that tool's bar instead, while it has anything
			// to work on, and that tool's button stays lit while it is active.
			virtual std::string StripOf() const { return std::string(); }
			// The commands the strip offers beside the sub-tools, good whether
			// or not the tool is the active one; null for none.
			virtual ToolOptions* StripCommands() { return nullptr; }
			// What this tool sets by number, which the editor's transform panel
			// shows while it is active (Pivot's pivot); null for nothing.
			virtual ITransformTarget* TransformTarget(IVoxelEditContext&) { return nullptr; }
			// The step the panel's Location holds to while it is Ctrl-dragged: a
			// tool whose Step the user picks (GizmoTool) answers with that, so the
			// panel and the tool's own dragging agree. A whole voxel otherwise.
			virtual float TransformStep() const { return 1.0F; }

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
