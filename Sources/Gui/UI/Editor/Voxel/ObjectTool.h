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

#include "GizmoTool.h"

namespace spades {
	namespace gui {
		/**
		 * Object mode's one tool: a click picks the object of the scene under
		 * the cursor (see IObjectModeHost), and the gizmo on the picked one
		 * carries every handle at once, arrows to move it, rings to turn it and
		 * cubes to scale it. Each drag is one step of the scene's history.
		 */
		class ObjectTool : public GizmoTool {
		public:
			ObjectTool();
			const char* Label() const override { return "Transform"; }
			std::string Hint(IVoxelEditContext&) override;
			// The picked object's position, rotation and scale, set by number.
			ITransformTarget* TransformTarget(IVoxelEditContext& ed) override;
		};
	} // namespace gui
} // namespace spades
