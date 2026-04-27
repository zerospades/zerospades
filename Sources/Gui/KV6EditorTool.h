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

			// Optional sub-tools, shown in a secondary toolbar under the main one
			// while this tool is active (e.g. Select's Point / Rect / By-Colour).
			virtual int SubToolCount() const { return 0; }
			virtual const char* SubToolLabel(int) const { return ""; }
			virtual int SubTool() const { return 0; }
			virtual void SetSubTool(int) {}

			// `button` is "LeftMouseButton" / "RightMouseButton".
			virtual void OnPointerDown(KV6EditorView&, const std::string& button) {}
			virtual void OnPointerUp(KV6EditorView&, const std::string& button) {}
			virtual void OnPointerDrag(KV6EditorView&) {}
			virtual void OnKey(KV6EditorView&, const std::string& key, bool down) {}

			// 3D preview, drawn between StartScene and EndScene.
			virtual void DrawScene(KV6EditorView&) {}
			// 2D overlay (tool sub-UI / status), drawn over the scene.
			virtual void DrawOverlay(KV6EditorView&) {}
		};
	} // namespace gui
} // namespace spades
