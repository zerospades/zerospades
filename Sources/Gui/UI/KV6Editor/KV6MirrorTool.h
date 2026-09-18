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
		 * The X/Y/Z mirror toggles, grouped under "Mirror".
		 *
		 * Every tool whose edits reflect shows them, so which axes are on stays
		 * in sight (and at hand) while drawing. They only show the editor's
		 * mirror state: sync them in UpdateOptions and route their clicks here.
		 */
		void AddMirrorToggles(ToolOptions& options);
		void SyncMirrorToggles(ToolOptions& options, IEditorContext& ed);
		/** Applies a clicked toggle to the editor; false if `id` is not one of them. */
		bool ApplyMirrorToggle(IEditorContext& ed, const std::string& id, bool value);

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

			ToolOptions* Options() override { return &options; }
			void UpdateOptions(IEditorContext& ed) override;
			void OnOptionToggled(IEditorContext& ed, const std::string& id, bool value) override;
			void OnAction(IEditorContext& ed, const std::string& id) override;

		private:
			ToolOptions options; // X/Y/Z toggles, Reset to Pivot, the plane readout
		};
	} // namespace gui
} // namespace spades
