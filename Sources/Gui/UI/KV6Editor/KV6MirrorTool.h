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

#include "KV6ContainerTool.h"

namespace spades {
	namespace gui {
		/**
		 * The UI over the editor's mirror state: which axes reflect, and where the
		 * planes sit.
		 *
		 * The state itself lives on the editor, not here, so an edit mirrors
		 * whichever tool made it — this tool only turns axes on and moves the
		 * planes. Its one sub-tool, Move, is the plane gizmo; Reset to Pivot is a
		 * one-shot action beside the X/Y/Z toggles.
		 */
		class MirrorTool : public ContainerTool {
		public:
			MirrorTool();
			const char* Label() const override { return "Mirror"; }

			void OnActivate(IEditorContext& ed) override;
			ToolOptions* Options() override { return &options; }
			void OnOptionToggled(IEditorContext& ed, const std::string& id, bool value) override;
			void OnAction(IEditorContext& ed, const std::string& id) override;
			void DrawScene(IEditorContext& ed) override;

		private:
			ToolOptions options; // X/Y/Z toggles, Reset to Pivot, the plane readout
		};
	} // namespace gui
} // namespace spades
