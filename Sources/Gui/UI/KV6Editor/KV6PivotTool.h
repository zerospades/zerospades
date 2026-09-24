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

#include "KV6GizmoTool.h"

namespace spades {
	namespace gui {
		// Set the model pivot: a draggable gizmo (steps down to 0.1), and an X, Y
		// and Z box on the sub-toolbar, which show where the pivot is and are
		// typed into or stepped to move it.
		class PivotTool : public GizmoTool {
		public:
			PivotTool();
			const char* Label() const override { return "Pivot"; }
			// Refresh the boxes from the live pivot.
			void UpdateOptions(IEditorContext&) override;
			void OnOptionNumberChanged(IEditorContext&, const std::string& id, float value,
			                           bool committed) override;

		private:
			// The pivot is float-valued, and its gizmo steps down to 0.1, so the
			// boxes are written to match what the finest drag can produce.
			static constexpr int kDecimals = 1;
			// Which axis `id` names, or -1 if it names none of them.
			static int AxisOf(const std::string& id);
		};
	} // namespace gui
} // namespace spades
