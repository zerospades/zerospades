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
		// Select voxels: Voxel (single), Box (box region), By Colour. Its bar holds
		// the commands that act on the selection, clipboard included.
		class SelectTool : public ContainerTool {
		public:
			SelectTool();
			const char* Label() const override { return "Select"; }
			EditorRole Role() const override { return EditorRole::Select; }

			ToolOptions* Options() override;
			// Commands with nothing to act on are greyed out.
			void UpdateOptions(IEditorContext& ed) override;
			void OnAction(IEditorContext& ed, const std::string& id) override;

			static void SelectAll(IEditorContext& ed);

		private:
			ToolOptions options;
		};
	} // namespace gui
} // namespace spades
