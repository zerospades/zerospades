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

	/**
	 * A scriptable editor tool.
	 *
	 * The C++ editor drives these callbacks while the tool is active; the tool
	 * queries and edits the model through the bound `EditorContext`. Register a
	 * tool by providing a factory the C++ SubToolRegistry can call (see
	 * CylinderTool.as).
	 */
	interface EditorTool {
		// Toolbar label (read once when the tool is created).
		string Label();

		void OnActivate(EditorContext@ ctx);
		void OnDeactivate(EditorContext@ ctx);

		// button/phase are EditorButton / EditorPhase values.
		void OnPointer(EditorContext@ ctx, int button, int phase, bool alt, bool ctrl, bool shift);
		void OnKey(EditorContext@ ctx, string key, bool down);

		// Abort an in-progress operation (Esc); return true if it was consumed.
		bool OnEscape(EditorContext@ ctx);

		// 3D preview, drawn each frame while active.
		void DrawScene(EditorContext@ ctx);
	}

}
