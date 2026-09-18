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

#include <string>

#include "KV6ToolEvent.h"
#include "KV6ToolOptions.h"

namespace spades {
	namespace gui {
		class IEditorContext;

		// What a top-level tool does with cells, so sub-tools (incl. scripted ones)
		// can apply through IEditorContext::ApplyCells without knowing their host.
		// The right button does the inverse of the left (erase, deselect); Paint
		// has no inverse, so the editor keeps the right button from its sub-tools.
		enum class EditorRole { Edit, Select, Paint };

		/**
		 * An editor tool (draw, select, ...).
		 *
		 * Tools receive pointer/key events while active and may draw their own 3D
		 * preview (between StartScene/EndScene) and 2D overlay. They operate on the
		 * editor through the `IEditorContext` seam, so adding a tool is just a new
		 * subclass registered in `KV6EditorView`'s tool list.
		 */
		class EditorTool {
		public:
			virtual ~EditorTool() {}

			// Short label shown on the toolbar button.
			virtual const char* Label() const = 0;

			// Whether this (top-level) tool edits voxels or builds a selection.
			// `ApplyCells` routes by the active tool's role.
			virtual EditorRole Role() const { return EditorRole::Edit; }

			virtual void OnActivate(IEditorContext&) {}
			virtual void OnDeactivate(IEditorContext&) {}

			// Abort an in-progress operation (Esc). Returns true if it consumed the
			// key (so the editor doesn't also open the pause menu).
			virtual bool OnEscape(IEditorContext&) { return false; }

			// Abandon a gesture in progress (a drag) without applying it. The editor
			// calls this before it acts behind the tool's back (a shortcut, a dialog
			// taking the input), so no gesture outlives the state it started from.
			virtual void CancelInteraction(IEditorContext&) {}

			// A command changed the document or the edit state (an edit, a shortcut,
			// an option, undo, redo). A tool holding anything derived from them
			// refreshes it here: a gizmo drops its drag, the Mirror tool its toggles.
			virtual void OnDocumentChanged(IEditorContext&) {}

			// What the mouse and keys do in this tool right now, in "  |  "
			// separated parts ("[LMB] place  |  [RMB] delete"). The editor shows
			// it under the viewport after the tool's name; empty shows the name only.
			virtual std::string Hint(IEditorContext&) { return std::string(); }

			// Declarative options shown in the secondary toolbar next to this tool's
			// sub-tools (e.g. Mirror's axis toggles, Draw's colour swatch). Returning
			// null means the tool has no options. The editor renders and hit-tests
			// whatever is listed, so tools never touch the toolbar code directly.
			virtual ToolOptions* Options() { return nullptr; }

			// Called just before the options are drawn: bring any option that
			// shows editor state (a readout, the mirror toggles, a command that
			// needs a selection) up to date with it.
			virtual void UpdateOptions(IEditorContext&) {}

			// A ToolOption of type Bool was clicked; it has already been flipped to
			// `value`. Tools that mirror a toggle into other state react here.
			virtual void OnOptionToggled(IEditorContext&, const std::string& id, bool value) {
				(void)id;
				(void)value;
			}

			// A ToolOption of type Action was clicked; `id` is that option's id.
			virtual void OnAction(IEditorContext&, const std::string& id) { (void)id; }

			// Optional sub-tools, shown in a secondary toolbar under the main one
			// while this tool is active (e.g. Select's Voxel / Box / By Colour).
			virtual int SubToolCount() const { return 0; }
			virtual const char* SubToolLabel(int) const { return ""; }
			// The sub-tool itself, for callers that must find one by what it is
			// rather than by its label; null when out of range.
			virtual EditorTool* SubTool(int) { return nullptr; }
			virtual int ActiveSubTool() const { return 0; }
			virtual void SetSubTool(IEditorContext&, int) {}

			// All pointer activity (press, release, move, drag) arrives here; the
			// phase and button live on the event.
			virtual void OnPointer(IEditorContext&, const PointerInput&) {}
			virtual void OnKey(IEditorContext&, const KeyInput&) {}

			// 3D preview, drawn between StartScene and EndScene.
			virtual void DrawScene(IEditorContext&) {}
			// 2D overlay (tool sub-UI / status), drawn over the scene.
			virtual void DrawOverlay(IEditorContext&) {}
		};
	} // namespace gui
} // namespace spades
