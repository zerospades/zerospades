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
#include "SelectSubTools.h"

namespace spades {
	namespace gui {
		/**
		 * The scene's objects, as Rect and Brush select them in Object mode,
		 * through the host's scene (IObjectModeHost). Once something is
		 * selected, the gizmo shows: Transform takes over.
		 */
		class ObjectsOnScreen final : public IScreenSelectable {
		public:
			int TakeOnScreen(IVoxelEditContext& ed, const std::function<bool(const Vector2&)>& inside,
			                 bool through, bool removing) override;
			bool TakeAtCursor(IVoxelEditContext& ed, bool removing) override;
			void Done(IVoxelEditContext& ed, bool removing) override;
			const char* Noun() const override { return "objects"; }
		};

		/**
		 * Picks objects by clicking (Select's "Click" in Object mode): a click
		 * selects the object under it alone, Shift or Ctrl adds or takes it
		 * away, and a click on nothing selects nothing. Once something is
		 * selected, Transform takes over with its gizmo.
		 */
		class ObjectClickSubTool : public VoxelTool {
		public:
			const char* Label() const override { return "Click"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
		};

		/**
		 * Object mode's Select: the scene's objects by a click, a rectangle or
		 * a brush (the same Rect and Brush as Edit mode's, given the objects),
		 * which hands over to Transform as soon as something is selected.
		 */
		class ObjectSelectTool : public ContainerTool {
		public:
			ObjectSelectTool();
			const char* Label() const override { return "Select"; }
			ToolOptions* Options() override { return &options; }
			void UpdateOptions(IVoxelEditContext& ed) override;
			void OnOptionToggled(IVoxelEditContext& ed, const std::string& id, bool value) override;
			void OnOptionNumberChanged(IVoxelEditContext& ed, const std::string& id, float value,
			                           bool committed) override;

		private:
			ToolOptions options;
			ScreenSelectSettings screen;
			ObjectsOnScreen objects;
			int rectIndex = -1, brushIndex = -1;
		};

		/** The ids Object mode's own tools go by, for SwitchTool. */
		extern const char* const kObjectSelectToolId;
		extern const char* const kObjectTransformToolId;
	} // namespace gui
} // namespace spades
