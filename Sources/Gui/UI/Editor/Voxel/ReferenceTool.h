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

#include "ContainerTool.h"

namespace spades {
	namespace gui {
		/**
		 * Sets up the images to model from: one across each axis (X, Y and Z,
		 * its sub-tools), each loaded from a file. The selected one carries a
		 * gizmo that moves, turns and scales it; its bar types where it is and
		 * how large, fades, mirrors, straightens or hides it. The images stay
		 * drawn whatever the tool; this one only places them, and nothing it
		 * does is an undo step.
		 */
		class ReferenceTool : public ContainerTool {
		public:
			ReferenceTool();
			const char* Label() const override { return "Reference"; }
			ToolOptions* Options() override { return &options; }
			void UpdateOptions(IVoxelEditContext& ed) override;
			void OnOptionToggled(IVoxelEditContext& ed, const std::string& id, bool value) override;
			void OnOptionNumberChanged(IVoxelEditContext& ed, const std::string& id, float value,
			                           bool committed) override;
			void OnAction(IVoxelEditContext& ed, const std::string& id) override;
			std::string Hint(IVoxelEditContext& ed) override;

		private:
			ToolOptions options;
		};
	} // namespace gui
} // namespace spades
