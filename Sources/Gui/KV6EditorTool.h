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
		class KV6EditorView;

		/**
		 * An editor tool (draw, select, ...).
		 *
		 * Tools receive pointer/key events while active and may draw their own 3D
		 * preview (between StartScene/EndScene) and 2D overlay. They operate on the
		 * editor through `KV6EditorView`'s public tool API, so adding a tool is just
		 * a new subclass registered in `KV6EditorView`'s tool list.
		 */
		class EditorTool {
		public:
			virtual ~EditorTool() {}

			// Short label shown on the toolbar button.
			virtual const char* Label() const = 0;

			virtual void OnActivate(KV6EditorView&) {}
			virtual void OnDeactivate(KV6EditorView&) {}

			// Abort an in-progress operation (Esc). Returns true if it consumed the
			// key (so the editor doesn't also open the pause menu).
			virtual bool OnEscape(KV6EditorView&) { return false; }

			// Declarative options shown in the secondary toolbar next to this tool's
			// sub-tools (e.g. Draw's mirror toggles and colour swatch). Returning
			// null means the tool has no options. The editor renders and hit-tests
			// whatever is listed, so tools never touch the toolbar code directly.
			virtual ToolOptions* Options() { return nullptr; }

			// Optional sub-tools, shown in a secondary toolbar under the main one
			// while this tool is active (e.g. Select's Point / Rect / By-Colour).
			virtual int SubToolCount() const { return 0; }
			virtual const char* SubToolLabel(int) const { return ""; }
			virtual int ActiveSubTool() const { return 0; }
			virtual void SetSubTool(KV6EditorView&, int) {}

			// All pointer activity (press, release, move, drag) arrives here; the
			// phase and button live on the event.
			virtual void OnPointer(KV6EditorView&, const PointerInput&) {}
			virtual void OnKey(KV6EditorView&, const KeyInput&) {}

			// 3D preview, drawn between StartScene and EndScene.
			virtual void DrawScene(KV6EditorView&) {}
			// 2D overlay (tool sub-UI / status), drawn over the scene.
			virtual void DrawOverlay(KV6EditorView&) {}
		};
	} // namespace gui
} // namespace spades
