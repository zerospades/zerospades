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

namespace spades {

	// Pointer button passed to EditorTool::OnPointer (mirrors C++ PointerButton).
	enum EditorButton { ButtonNone, ButtonLeft, ButtonRight, ButtonMiddle }

	// Pointer phase passed to EditorTool::OnPointer (mirrors C++ PointerPhase).
	enum EditorPhase { PhaseDown, PhaseUp, PhaseMove, PhaseDrag }

	// Containers a tool wants to appear in, as a bitmask returned by Targets().
	enum EditorTarget { TargetDraw = 1, TargetSelect = 2, TargetPaint = 4 }

	/**
	 * A scriptable editor tool.
	 *
	 * The C++ editor drives these callbacks while the tool is active; the tool
	 * queries and edits the model through the bound `EditorContext`. Every class
	 * implementing this interface with a no-argument constructor is found and
	 * registered on its own (see CylinderTool.as).
	 *
	 * Derive tools from EditorToolBase rather than implementing this directly:
	 * it gives every method a default, so a tool overrides only what it needs
	 * and keeps compiling as methods are added here.
	 */
	interface EditorTool {
		// Toolbar label (read once when the tool is created).
		string Label();

		// Which containers to appear in, as an EditorTarget bitmask (e.g.
		// EditorTarget::TargetDraw | EditorTarget::TargetSelect). Read once at
		// discovery; the same class is instantiated separately per container.
		int Targets();

		void OnActivate(EditorContext@ ctx);
		void OnDeactivate(EditorContext@ ctx);

		// button/phase are EditorButton / EditorPhase values.
		void OnPointer(EditorContext@ ctx, int button, int phase, bool alt, bool ctrl, bool shift);
		void OnKey(EditorContext@ ctx, string key, bool down);

		// What Escape would back out of now, as a short phrase for the hint line
		// ("cancel the cylinder"); empty when nothing is in progress. When Escape
		// reaches the tool, the editor calls OnEscape to do it.
		string EscapeLabel(EditorContext@ ctx);
		void OnEscape(EditorContext@ ctx);

		// What the mouse and keys do right now, in "  |  " separated parts; the
		// editor shows it under the viewport after the tool's name. Queried each
		// frame, so it can follow the tool's progress. Empty for no hint.
		string Hint(EditorContext@ ctx);

		// 3D preview, drawn each frame while active.
		void DrawScene(EditorContext@ ctx);
	}

	/**
	 * EditorTool with a default for every method: no label of its own, every
	 * container, and nothing done on any event. Being abstract, it is never
	 * registered as a tool itself.
	 */
	abstract class EditorToolBase : EditorTool {
		string Label() { return "Script"; }
		int Targets() {
			return int(EditorTarget::TargetDraw) | int(EditorTarget::TargetSelect) |
			       int(EditorTarget::TargetPaint);
		}
		void OnActivate(EditorContext@ ctx) {}
		void OnDeactivate(EditorContext@ ctx) {}
		void OnPointer(EditorContext@ ctx, int button, int phase, bool alt, bool ctrl, bool shift) {}
		void OnKey(EditorContext@ ctx, string key, bool down) {}
		string EscapeLabel(EditorContext@ ctx) { return ""; }
		void OnEscape(EditorContext@ ctx) {}
		string Hint(EditorContext@ ctx) { return ""; }
		void DrawScene(EditorContext@ ctx) {}
	}

}
