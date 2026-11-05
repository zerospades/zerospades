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

#include <Core/Math.h>

#include "VoxelTool.h"

namespace spades {
	namespace gui {
		/**
		 * What Select's selections on the screen share, set on its bar: the
		 * brush's size, and whether they reach through the volume to the
		 * voxels hidden behind (X-Ray) or take only those in sight.
		 */
		struct ScreenSelectSettings {
			float diameter = 40.0F; // screen pixels
			bool through = false;
			static constexpr float kMinDiameter = 4.0F;
			static constexpr float kMaxDiameter = 2000.0F;
		};

		/**
		 * Selects a flat face of the model (Select's "Plane"), as a modeller
		 * selects coplanar faces: a click on a voxel's face takes every voxel
		 * reached from it face to face in that plane whose face that way shows;
		 * a face another voxel covers is not part of it. LMB adds, RMB removes.
		 */
		class PlaneSelectSubTool : public VoxelTool {
		public:
			const char* Label() const override { return "Plane"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			void DrawScene(IVoxelEditContext&) override;
		};

		/**
		 * Selects what a rectangle dragged on the screen takes in (Select's
		 * "Rect"): the voxels in sight there, or with X-Ray every one. A click
		 * without a drag takes the voxel under it. LMB adds, RMB removes;
		 * Escape gives the rectangle up.
		 */
		class RectSelectSubTool : public VoxelTool {
		public:
			explicit RectSelectSubTool(const ScreenSelectSettings& settings) : settings(settings) {}
			const char* Label() const override { return "Rect"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			std::string EscapeLabel(IVoxelEditContext&) override;
			void OnEscape(IVoxelEditContext&) override { dragging = false; }
			void CancelInteraction(IVoxelEditContext&) override { dragging = false; }
			void DrawOverlay(IVoxelEditContext&) override;

		private:
			const ScreenSelectSettings& settings;
			bool dragging = false;
			bool removing = false;
			Vector2 start = MakeVector2(0.0F, 0.0F), end = MakeVector2(0.0F, 0.0F);
			AABB2 Rect() const;
		};

		/**
		 * Paints a selection with a round brush on the screen (Select's
		 * "Brush"), of a diameter set on the bar or with [ and ]: the voxels in
		 * sight under it, or with X-Ray every one. A stroke takes all it sweeps
		 * over, as one step. LMB adds, RMB removes.
		 */
		class BrushSelectSubTool : public VoxelTool {
		public:
			explicit BrushSelectSubTool(ScreenSelectSettings& settings) : settings(settings) {}
			const char* Label() const override { return "Brush"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			void OnKey(IVoxelEditContext&, const KeyInput&) override;
			void CancelInteraction(IVoxelEditContext&) override { painting = false; }
			void DrawOverlay(IVoxelEditContext&) override;

		private:
			ScreenSelectSettings& settings;
			bool painting = false;
			bool removing = false;
			Vector2 last = MakeVector2(0.0F, 0.0F);
			/** Takes what the brush sweeps from `from` to `to`. */
			void Sweep(IVoxelEditContext& ed, const Vector2& from, const Vector2& to);
		};
	} // namespace gui
} // namespace spades
