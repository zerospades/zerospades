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

#include <Core/Math.h>
#include <Gui/UI/Components/Gizmo/GizmoView.h>

namespace spades {
	namespace gui {
		class TransformGizmo;

		/**
		 * The editor seam tools operate through: what every editor offers its
		 * tools, whatever it edits.
		 *
		 * Tools never touch the editor view directly; they query and mutate the
		 * editor through this narrow interface, which the editor view implements.
		 * An editor adds what its documents need in an interface deriving from
		 * this one (a voxel editor's is `IVoxelEditContext`), and its tools work
		 * through that. Keeping it abstract decouples tools from the host.
		 */
		class IEditorContext {
		public:
			virtual ~IEditorContext() {}

			// --- Camera / cursor ----------------------------------------------
			virtual Vector3 ViewDir() const = 0;
			virtual const Vector2& CursorPos() const = 0;

			// --- Overlay drawing ----------------------------------------------
			virtual void DrawLine3D(const Vector3& a, const Vector3& b, const Vector4& color) = 0;

			// --- Transform gizmo ----------------------------------------------
			/** The live 3D view, for a gizmo to pick and drag against. */
			virtual GizmoView GetGizmoView() const = 0;
			/** Draws `gizmo` over the finished scene; call from a tool's DrawOverlay. */
			virtual void DrawGizmo(const TransformGizmo& gizmo) = 0;

			// --- Feedback -----------------------------------------------------
			// A transient message: what an action did, or why it did nothing.
			// What a tool's buttons and keys do belongs in BasicEditorTool::Hint.
			virtual void SetStatus(const std::string&) = 0;

			// Edits made through a context are journaled automatically: the editor
			// merges everything done during one user action (a press to its
			// release, a key press) into a single undo step, so a stroke or a
			// multi-step edit undoes at once without the tool grouping it.
		};
	} // namespace gui
} // namespace spades
