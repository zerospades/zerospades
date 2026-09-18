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

#include <cstdint>

#include <Core/Math.h>

#include "GizmoView.h"

namespace spades {
	namespace gui {
		class GizmoCanvas;
		struct GizmoFrame; // the gizmo laid out for one view (TransformGizmo.cpp)

		/** Every part of a transform gizmo that can be grabbed. */
		enum class GizmoHandle {
			None = -1,
			// Arrows along one axis.
			TranslateX,
			TranslateY,
			TranslateZ,
			// Squares moving in a plane, named after the plane.
			TranslateYZ,
			TranslateZX,
			TranslateXY,
			// The centre circle: moves in the plane facing the viewer.
			TranslateView,
			// Rings turning about one axis.
			RotateX,
			RotateY,
			RotateZ,
			// The ring around the others: turns about the line of sight.
			RotateView,
			// Free rotation by dragging inside the rings.
			RotateTrackball,
			// Cubes stretching along one axis.
			ScaleX,
			ScaleY,
			ScaleZ,
			// Outlined squares stretching within a plane, named after the plane.
			ScaleYZ,
			ScaleZX,
			ScaleXY,
			// The outermost ring: grows or shrinks evenly.
			ScaleUniform,
		};

		/** A set of gizmo handles. */
		class GizmoHandleSet {
		public:
			GizmoHandleSet() = default;

			static GizmoHandleSet None() { return GizmoHandleSet(); }
			static GizmoHandleSet Of(GizmoHandle handle);
			static GizmoHandleSet Translation(); // arrows, squares, centre circle
			static GizmoHandleSet Rotation();    // axis rings, view ring, trackball
			static GizmoHandleSet Scaling();     // axis cubes, squares, uniform ring
			static GizmoHandleSet All();

			bool Contains(GizmoHandle handle) const;
			bool Intersects(const GizmoHandleSet& other) const { return (bits & other.bits) != 0; }
			bool IsEmpty() const { return bits == 0; }

			GizmoHandleSet operator|(const GizmoHandleSet& o) const {
				return GizmoHandleSet(bits | o.bits);
			}
			GizmoHandleSet operator&(const GizmoHandleSet& o) const {
				return GizmoHandleSet(bits & o.bits);
			}
			bool operator==(const GizmoHandleSet& o) const { return bits == o.bits; }
			bool operator!=(const GizmoHandleSet& o) const { return bits != o.bits; }

		private:
			explicit GizmoHandleSet(uint32_t bits) : bits(bits) {}
			uint32_t bits = 0;
		};

		/** Where the gizmo sits and which way its axes point. */
		struct GizmoPose {
			Vector3 position = MakeVector3(0.0F, 0.0F, 0.0F);
			// Orthonormal. World axes give a global gizmo; an object's axes give a
			// local one.
			Vector3 axes[3] = {MakeVector3(1.0F, 0.0F, 0.0F), MakeVector3(0.0F, 1.0F, 0.0F),
			                   MakeVector3(0.0F, 0.0F, 1.0F)};
		};

		/** Snap increments per kind of change; zero leaves that kind free. */
		struct GizmoSnap {
			float translation = 0.0F; // world units, per pose axis
			float rotation = 0.0F;    // radians
			float scale = 0.0F;       // added to a factor of 1 (0.1 gives 1.0, 1.1, ...)
		};

		/** A change made by a drag, about the gizmo's position. */
		struct GizmoTransform {
			Vector3 translation = MakeVector3(0.0F, 0.0F, 0.0F);
			// Right-handed rotation in world space: a positive angle about an axis
			// turns counter-clockwise when that axis points at the viewer, as
			// `Quaternion::Apply` sees it. `Quaternion::MakeRotation` builds the
			// opposite sense, so don't mix it into this.
			Quaternion rotation = Quaternion(0.0F, 0.0F, 0.0F, 1.0F);
			// Factors along the pose axes.
			Vector3 scale = MakeVector3(1.0F, 1.0F, 1.0F);

			bool IsIdentity() const;
		};

		/**
		 * An interactive translate / rotate / scale manipulator, in the style of
		 * Blender's and FreeCAD's transform gizmos, drawn as a 2D overlay.
		 *
		 * Any mix of handles can be shown at once: arrows, squares and the centre
		 * circle move; axis rings, the view ring and the trackball turn; axis
		 * cubes, outlined squares and the uniform ring scale. The layout spreads
		 * out as kinds are combined so no two handles sit on top of each other.
		 * The gizmo keeps a constant size on screen, hides handles that point
		 * straight at the viewer, and grabs whichever handle is nearest the
		 * cursor on screen.
		 *
		 * It never touches the thing being transformed. A drag reports a
		 * transform, both its total since the grab and its step since the last
		 * pointer move, and the owner applies whichever suits it.
		 *
		 * Contract: before every call, SetPose() must give where the handled
		 * thing is *now* — its pose at the grab with everything this drag
		 * reported already applied. Drags are measured against that live pose,
		 * so the owner may shift its coordinates mid-drag (an undo that grows a
		 * volume, say) without the gizmo jumping.
		 */
		class TransformGizmo {
		public:
			TransformGizmo();

			// --- Configuration ------------------------------------------------
			/** Handles that are drawn. Defaults to every translation handle. */
			void SetVisibleHandles(const GizmoHandleSet& handles) { visible = handles; }
			const GizmoHandleSet& VisibleHandles() const { return visible; }
			/**
			 * Handles that respond to the pointer. A visible handle that is not
			 * enabled is drawn greyed out. Defaults to every translation handle.
			 */
			void SetEnabledHandles(const GizmoHandleSet& handles) { enabled = handles; }
			const GizmoHandleSet& EnabledHandles() const { return enabled; }
			void SetSnap(const GizmoSnap& snap) { this->snap = snap; }
			const GizmoSnap& Snap() const { return snap; }
			void SetPose(const GizmoPose& pose) { this->pose = pose; }
			const GizmoPose& Pose() const { return pose; }
			/** On-screen radius of the rotation rings, in pixels; sizes the rest. */
			void SetSize(float pixels);

			// --- Interaction (screen pixels) ----------------------------------
			/** The handle a press at `cursor` would grab, or None. */
			GizmoHandle HandleAt(const GizmoView& view, const Vector2& cursor) const;
			/** Highlights the handle under `cursor`. Ignored while dragging. */
			void Hover(const GizmoView& view, const Vector2& cursor);
			/** Grabs the handle under `cursor`; false if there is none. */
			bool Begin(const GizmoView& view, const Vector2& cursor);
			/** Follows the pointer; true when the reported transform changed. */
			bool Drag(const GizmoView& view, const Vector2& cursor);
			/** Releases the handle and returns the drag's total transform. */
			GizmoTransform End();
			/**
			 * Abandons the drag and returns the step that undoes everything it
			 * reported, for owners that applied the steps as they came.
			 */
			GizmoTransform Cancel();

			bool IsDragging() const { return active != GizmoHandle::None; }
			GizmoHandle ActiveHandle() const { return active; }
			GizmoHandle HoveredHandle() const { return hovered; }
			/** The whole change since Begin; identity when not dragging. */
			const GizmoTransform& Total() const { return total; }
			/** The change made by the last Drag; identity when not dragging. */
			const GizmoTransform& Step() const { return step; }

			void Draw(GizmoCanvas& canvas, const GizmoView& view) const;

		private:
			GizmoHandleSet visible;
			GizmoHandleSet enabled;
			GizmoSnap snap;
			GizmoPose pose;
			float sizePixels;

			GizmoHandle hovered = GizmoHandle::None;
			GizmoHandle active = GizmoHandle::None;
			GizmoTransform total;
			GizmoTransform step;

			// What a drag needs to remember from the moment of the grab.
			struct DragState {
				Vector3 axes[3];            // pose axes at the grab
				Vector2 grabCursor;         // cursor at the grab
				Vector2 lastCursor;         // cursor at the previous Drag
				Vector3 planeNormal;        // plane the cursor ray is cast against
				Vector3 grabOffset;         // grab point minus centre, on that plane
				Vector3 rotationAxis;       // axis of a ring or view-ring turn
				Vector3 startDirection;     // centre-to-grab direction on the ring's plane
				Vector3 lastDirection;      // the same at the previous Drag
				bool alongScreen = false;   // ring seen edge-on: turn by screen movement
				Vector2 screenTangent;      // screen direction of a positive turn
				float ringPixels = 0.0F;    // ring radius on screen at the grab
				float angle = 0.0F;         // unsnapped turn so far
				float appliedAngle = 0.0F;  // snapped turn reported so far
				float grabDistance = 0.0F;  // scale: centre-to-grab distance
				Vector3 grabDirection;      // plane scale: direction of that distance
			} drag;

			GizmoFrame MakeFrame(const GizmoView& view) const;
			bool IsHot(GizmoHandle handle) const { return handle == hovered || handle == active; }
			bool IsUsable(GizmoHandle handle, const GizmoFrame& frame) const;
			Vector4 HandleColor(GizmoHandle handle, const GizmoFrame& frame) const;

			bool BeginTranslate(const GizmoView& view, const GizmoFrame& frame,
			                    const Vector2& cursor);
			bool BeginRotate(const GizmoView& view, const GizmoFrame& frame, const Vector2& cursor);
			bool BeginScale(const GizmoView& view, const GizmoFrame& frame, const Vector2& cursor);
			bool DragTranslate(const GizmoView& view, const Vector2& cursor);
			bool DragRotate(const GizmoView& view, const Vector2& cursor);
			bool DragScale(const GizmoView& view, const Vector2& cursor);

			void DrawIdle(GizmoCanvas& canvas, const GizmoView& view,
			              const GizmoFrame& frame) const;
			void DrawDragging(GizmoCanvas& canvas, const GizmoView& view,
			                  const GizmoFrame& frame) const;
			// One handle at rest; `stretch` sizes a scale handle being dragged.
			void DrawHandle(GizmoCanvas& canvas, const GizmoView& view, const GizmoFrame& frame,
			                GizmoHandle handle, float stretch) const;
		};
	} // namespace gui
} // namespace spades
