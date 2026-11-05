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

#include <functional>

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
		 * What Rect and Brush select on the screen: voxels in Edit mode, the
		 * scene's objects in Object mode. The sub-tools say where on the screen
		 * and whether to add or take away; this decides what that takes.
		 */
		class IScreenSelectable {
		public:
			virtual ~IScreenSelectable() {}
			/**
			 * Selects what the view shows where `inside` says (screen pixels),
			 * or takes it out of the selection (`removing`): what is in sight,
			 * or with `through` all there. Returns how many it took.
			 */
			virtual int TakeOnScreen(IVoxelEditContext& ed,
			                         const std::function<bool(const Vector2&)>& inside, bool through,
			                         bool removing) = 0;
			/** A click: what is under the cursor; false when there is nothing. */
			virtual bool TakeAtCursor(IVoxelEditContext& ed, bool removing) = 0;
			/** A rectangle or a stroke is over. */
			virtual void Done(IVoxelEditContext& ed, bool removing) {
				(void)ed;
				(void)removing;
			}
			/** What it selects, in messages: "voxels". */
			virtual const char* Noun() const = 0;
		};

		/** The document's voxels, as Rect and Brush select them in Edit mode. */
		class VoxelsOnScreen final : public IScreenSelectable {
		public:
			int TakeOnScreen(IVoxelEditContext& ed, const std::function<bool(const Vector2&)>& inside,
			                 bool through, bool removing) override;
			bool TakeAtCursor(IVoxelEditContext& ed, bool removing) override;
			const char* Noun() const override { return "voxels"; }
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
		 * "Rect"): what is in sight there, or with X-Ray all of it. A click
		 * without a drag takes what is under it. LMB adds, RMB removes; Escape
		 * gives the rectangle up.
		 */
		class RectSelectSubTool : public VoxelTool {
		public:
			RectSelectSubTool(IScreenSelectable& target, const ScreenSelectSettings& settings)
			    : target(target), settings(settings) {}
			const char* Label() const override { return "Rect"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			std::string EscapeLabel(IVoxelEditContext&) override;
			void OnEscape(IVoxelEditContext&) override { dragging = false; }
			void CancelInteraction(IVoxelEditContext&) override { dragging = false; }
			void DrawOverlay(IVoxelEditContext&) override;

		private:
			IScreenSelectable& target;
			const ScreenSelectSettings& settings;
			bool dragging = false;
			bool removing = false;
			Vector2 start = MakeVector2(0.0F, 0.0F), end = MakeVector2(0.0F, 0.0F);
			AABB2 Rect() const;
		};

		/**
 * Paints a selection with a round brush on the screen (Select's
		 * "Brush"), of a diameter set on the bar or with [ and ]: what is in
		 * sight under it, or with X-Ray all of it. A stroke takes all it sweeps
		 * over, as one step. LMB adds, RMB removes.
		 */
		class BrushSelectSubTool : public VoxelTool {
		public:
			BrushSelectSubTool(IScreenSelectable& target, ScreenSelectSettings& settings)
			    : target(target), settings(settings) {}
			const char* Label() const override { return "Brush"; }
			std::string Hint(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			void OnKey(IVoxelEditContext&, const KeyInput&) override;
			void CancelInteraction(IVoxelEditContext&) override { painting = false; }
			void DrawOverlay(IVoxelEditContext&) override;

		private:
			IScreenSelectable& target;
			ScreenSelectSettings& settings;
			bool painting = false;
			bool removing = false;
			Vector2 last = MakeVector2(0.0F, 0.0F);
			/** Takes what the brush sweeps from `from` to `to`. */
			void Sweep(IVoxelEditContext& ed, const Vector2& from, const Vector2& to);
		};
	} // namespace gui
} // namespace spades
