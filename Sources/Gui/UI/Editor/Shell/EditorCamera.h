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

#include <Client/SceneDefinition.h>
#include <Core/Math.h>
#include <Gui/UI/Components/Gizmo/GizmoView.h>

namespace spades {
	namespace gui {
		/**
		 * An editor's orbiting camera: it looks at a point from a distance, and
		 * is steered the way the editors all steer theirs.
		 *
		 * The movement keys (cg_keyMoveForward, ..., cg_keyJump up, cg_keyCrouch
		 * down, cg_keySprint faster) carry the orbited point, the wheel button
		 * turns the view round it (panning with Shift), the wheel zooms, and a
		 * navigation cube click turns it smoothly to look from a side, and a
		 * framing (FlyToFrame, FlyHome) carries it there just as smoothly. Each
		 * frame `Scene` sets up the scene camera from it and remembers the view
		 * it made, so picking, projection, panning and gizmos all agree on
		 * every pixel with what was drawn.
		 */
		class EditorCamera {
		public:
			/** The near clip distance of the scene camera; anything drawn in 3D
			 *  over the scene clips against the same plane. */
			static constexpr float kNearPlane = 0.1F;

			/** Looks at `centre` from far enough to take in a subject `size` across. */
			void Frame(const Vector3& centre, float size);
			/** As Frame, but flying there smoothly, still looking from the same
			 *  angle. */
			void FlyToFrame(const Vector3& centre, float size);
			/** As FlyToFrame, also turning to the angle the camera starts at:
			 *  back to the view a document opens with. */
			void FlyHome(const Vector3& centre, float size);
			/** Carries the orbited point by `delta`: with a document relabelled by
			 *  as much, the view stays where it is on it. */
			void Shift(const Vector3& delta) {
				target += delta;
				flyTarget += delta;
			}

			// --- Input --------------------------------------------------------
			/** Follows the modifiers the movement depends on (Control and the
			 *  sprint key). Fed every key, even while a modal has the keyboard,
			 *  so none of them stays "held" after a release it swallowed. */
			void TrackModifiers(const std::string& key, bool down);
			/** Follows the movement keys; true when `key` was one, which nothing
			 *  else should then act on. `now` is the editor's clock. */
			bool MovementKey(const std::string& key, bool down, float now);
			/** A Ctrl shortcut is starting: Ctrl is also the default descend key,
			 *  so take back how far it moved the view during this press, and stop
			 *  descending for the rest of it. */
			void CancelCtrlDescent();
			/** Whether the wheel button is held, turning (or with Shift panning)
			 *  the view. */
			void SetLooking(bool on) { looking = on; }
			bool IsLooking() const { return looking; }
			/** Slides the orbited point along the line of sight to the depth of
			 *  `point`, leaving the view exactly as it is: the next turn goes
			 *  round what is there, not round a point left far off by flying. */
			void OrbitAtDepthOf(const Vector3& point);
			/** Turns the view round the orbited point by a mouse motion. */
			void Look(float dx, float dy);
			/** Slides the view along the screen axes by a mouse motion, so the
			 *  point under the cursor follows it at any zoom. */
			void Pan(float dx, float dy);
			/** Zooms by a wheel motion. */
			void Zoom(float wheel);
			/** Turns smoothly to look at the orbited point from `dir`. */
			void SnapToward(const Vector3& dir);
			/** Forgets held keys whose release a modal may swallow. */
			void ReleaseHeld();

			// --- Each frame ---------------------------------------------------
			/** Advances a snap in progress and, when `moving`, the movement keys'
			 *  motion at `speed` world units per second. */
			void Update(float dt, float now, float speed, bool moving);
			/** The scene camera for a `width` x `height` viewport at the screen's
			 *  top left, remembered as View(). Only the camera: what else the
			 *  scene holds is the editor's to fill in. */
			client::SceneDefinition Scene(float width, float height);
			/** The view of the last Scene. */
			const GizmoView& View() const { return view; }
			/** Far-plane / fog distance, scaled so zooming out never clips the scene. */
			float ViewDistance() const;

		private:
			// The angle the camera starts at, and FlyHome turns back to.
			static constexpr float kHomeYaw = -kQuarterPi;
			static constexpr float kHomePitch = -kPi * 0.30F;

			float yaw = kHomeYaw;
			float pitch = kHomePitch;
			float targetYaw = 0.0F, targetPitch = 0.0F; // a snap animates toward these
			bool snapping = false;
			Vector3 target = MakeVector3(0.0F, 0.0F, 0.0F);
			float distance = 56.0F;
			// A flight in progress carries `target` and `distance` toward these.
			// Moving, panning or zooming by hand takes over from it.
			bool flying = false;
			Vector3 flyTarget = MakeVector3(0.0F, 0.0F, 0.0F);
			float flyDistance = 0.0F;
			bool looking = false;

			bool keyFwd = false, keyBack = false, keyLeft = false, keyRight = false;
			bool keyUp = false, keyDown = false, keySprint = false, ctrlHeld = false;
			// When the descend key went down, for the grace period that keeps a
			// Ctrl chord from moving the camera at all.
			float descendPressTime = 0.0F;
			// How far the Ctrl-bound descend key moved the view during the current
			// Ctrl press; a Ctrl shortcut takes it back.
			Vector3 ctrlDescent = MakeVector3(0.0F, 0.0F, 0.0F);

			GizmoView view;

			Vector3 Forward() const;
			bool DescendKeyIsActive(float now) const;
			/** Starts turning smoothly to (`yaw`, `pitch`), the short way round. */
			void TurnTo(float yaw, float pitch);
		};
	} // namespace gui
} // namespace spades
