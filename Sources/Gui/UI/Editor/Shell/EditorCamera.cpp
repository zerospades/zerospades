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

#include "EditorCamera.h"

#include <algorithm>
#include <cmath>

#include <Core/Settings.h>

#include "EditorKeys.h"

SPADES_SETTING(cg_keyMoveForward);
SPADES_SETTING(cg_keyMoveBackward);
SPADES_SETTING(cg_keyMoveLeft);
SPADES_SETTING(cg_keyMoveRight);
SPADES_SETTING(cg_keyJump);
SPADES_SETTING(cg_keyCrouch);
SPADES_SETTING(cg_keySprint);

namespace spades {
	namespace gui {
		namespace {
			// Camera speed factor while the sprint key (cg_keySprint) is held.
			constexpr float kSprintMultiplier = 3.0F;

			// How long a descend key that doubles as a chord modifier (Ctrl, the
			// default cg_keyCrouch) must be held alone before it moves the camera.
			// Long enough that even an unhurried Ctrl+Z never nudges the view, while
			// a deliberate hold still starts descending without feeling stuck.
			constexpr float kDescendGracePeriod = 0.4F;

			// The far plane (and the fog that fades into it) has to sit beyond the
			// camera, or zooming out puts the whole subject behind it.
			constexpr float kViewDistanceFactor = 4.0F;
			constexpr float kMinViewDistance = 1000.0F;

			// How far a framed subject is seen from, per unit of its size.
			constexpr float kFrameDistancePerSize = 1.8F;
			constexpr float kMinDistance = 2.0F;
			constexpr float kMaxDistance = 1000.0F;
			// Radians turned per pixel of mouse motion while looking.
			constexpr float kLookSensitivity = 0.003F;
			// How fast a snap or a flight closes on its target, per second.
			constexpr float kSnapRate = 12.0F;
			// A flight lands once within this fraction of its distance of where it
			// is going: far below a voxel at any framing.
			constexpr float kFlyLandingFraction = 1.0e-3F;
			constexpr float kVerticalFovDegrees = 60.0F;

			// Whether the descend binding is Control, which also opens shortcuts.
			bool DescendIsControl() { return EditorKeyMatches(cg_keyCrouch, "Control"); }
		} // namespace

		void EditorCamera::Frame(const Vector3& centre, float size) {
			target = centre;
			distance = std::max(kMinDistance, size * kFrameDistancePerSize);
			flying = false;
		}

		void EditorCamera::FlyToFrame(const Vector3& centre, float size) {
			flyTarget = centre;
			flyDistance =
			  std::max(kMinDistance, std::min(kMaxDistance, size * kFrameDistancePerSize));
			flying = true;
		}

		void EditorCamera::FlyHome(const Vector3& centre, float size) {
			TurnTo(kHomeYaw, kHomePitch);
			FlyToFrame(centre, size);
		}

		Vector3 EditorCamera::Forward() const {
			float cp = cosf(pitch);
			return MakeVector3(cp * cosf(yaw), cp * sinf(yaw), -sinf(pitch));
		}

		float EditorCamera::ViewDistance() const {
			return std::max(kMinViewDistance, distance * kViewDistanceFactor);
		}

		void EditorCamera::TrackModifiers(const std::string& key, bool down) {
			if (key == "Control")
				ctrlHeld = down;
			if (EditorKeyMatches(cg_keySprint, key))
				keySprint = down;
		}

		bool EditorCamera::MovementKey(const std::string& key, bool down, float now) {
			if (EditorKeyMatches(cg_keyMoveForward, key)) { keyFwd = down; return true; }
			if (EditorKeyMatches(cg_keyMoveBackward, key)) { keyBack = down; return true; }
			if (EditorKeyMatches(cg_keyMoveLeft, key)) { keyLeft = down; return true; }
			if (EditorKeyMatches(cg_keyMoveRight, key)) { keyRight = down; return true; }
			if (EditorKeyMatches(cg_keyJump, key)) { keyUp = down; return true; }
			if (EditorKeyMatches(cg_keyCrouch, key)) {
				keyDown = down;
				if (down)
					descendPressTime = now; // starts the grace period
				ctrlDescent = MakeVector3(0, 0, 0); // a new press, or a finished one
				return true;
			}
			return false;
		}

		void EditorCamera::CancelCtrlDescent() {
			if (!DescendIsControl())
				return;
			target -= ctrlDescent;
			ctrlDescent = MakeVector3(0, 0, 0);
			keyDown = false;
		}

		void EditorCamera::OrbitAtDepthOf(const Vector3& point) {
			const Vector3 fwd = Forward();
			const Vector3 eye = target - fwd * distance;
			const float depth = Vector3::Dot(point - eye, fwd);
			// Nothing in front of the eye to turn round.
			if (!std::isfinite(depth) || depth <= 0.0F)
				return;
			// The eye stays put; only how far ahead of it the orbited point lies
			// changes, so nothing on screen moves.
			distance = std::max(kMinDistance, std::min(kMaxDistance, depth));
			target = eye + fwd * distance;
			flying = false;
		}

		void EditorCamera::Look(float dx, float dy) {
			snapping = false; // turning by hand cancels a snap
			yaw += dx * kLookSensitivity;
			pitch -= dy * kLookSensitivity;
			const float lim = kHalfPi - 0.01F;
			pitch = std::max(-lim, std::min(lim, pitch));
		}

		void EditorCamera::Pan(float dx, float dy) {
			if (!view.IsValid())
				return;
			// World units per screen pixel at the orbited point, so the point under
			// the cursor follows the drag at any zoom level.
			float unitsPerPixel = view.WorldPerPixel(target);
			target += view.right * (-dx * unitsPerPixel) + view.up * (dy * unitsPerPixel);
			flying = false;
		}

		void EditorCamera::Zoom(float wheel) {
			flying = false;
			distance = std::max(kMinDistance, std::min(kMaxDistance, distance * (1.0F + wheel * 0.1F)));
		}

		void EditorCamera::SnapToward(const Vector3& dir) {
			Vector3 f = dir * -1.0F; // camera forward = look toward the subject
			float tp, ty;
			if (std::fabs(f.z) > 0.999F) {
				tp = asinf(std::max(-1.0F, std::min(1.0F, -f.z)));
				ty = yaw; // looking straight up/down: keep heading
			} else {
				tp = asinf(-f.z);
				ty = atan2f(f.y, f.x);
			}
			TurnTo(ty, tp);
		}

		void EditorCamera::TurnTo(float ty, float tp) {
			// Shortest angular path for yaw.
			while (ty - yaw > kPi) ty -= kTwoPi;
			while (ty - yaw < -kPi) ty += kTwoPi;
			targetYaw = ty;
			targetPitch = tp;
			snapping = true;
		}

		void EditorCamera::ReleaseHeld() {
			keyFwd = keyBack = keyLeft = keyRight = keyUp = keyDown = false;
			ctrlDescent = MakeVector3(0, 0, 0);
			looking = false;
		}

		bool EditorCamera::DescendKeyIsActive(float now) const {
			// A descend key that cannot start a shortcut moves the camera at once.
			if (!DescendIsControl())
				return true;
			// Ctrl also opens Ctrl+S/C/X/V/Z/Y, so wait out the moment in which the
			// letter of a chord would arrive: the view then never moves for a
			// shortcut, and a deliberate hold still descends.
			return now - descendPressTime >= kDescendGracePeriod;
		}

		void EditorCamera::Update(float dt, float now, float speed, bool moving) {
			// Smoothly rotate toward a navigation cube view.
			if (snapping) {
				float k = std::min(1.0F, dt * kSnapRate);
				yaw += (targetYaw - yaw) * k;
				pitch += (targetPitch - pitch) * k;
				if (std::fabs(targetYaw - yaw) < 0.002F && std::fabs(targetPitch - pitch) < 0.002F) {
					yaw = targetYaw;
					pitch = targetPitch;
					snapping = false;
				}
			}
			// Carry the view toward a framing; the distance closes in proportion,
			// so a long way out reads as fast as a short one.
			if (flying) {
				float k = std::min(1.0F, dt * kSnapRate);
				target += (flyTarget - target) * k;
				distance *= powf(flyDistance / distance, k);
				const float landing = flyDistance * kFlyLandingFraction;
				if ((flyTarget - target).GetLength() < landing &&
				    std::fabs(flyDistance - distance) < landing) {
					target = flyTarget;
					distance = flyDistance;
					flying = false;
				}
			}
			if (!moving)
				return;

			Vector3 fwd = Forward();
			Vector3 up = MakeVector3(0.0F, 0.0F, -1.0F);
			// Cross(fwd, up) normalised, derived from the heading so it stays valid
			// when looking straight up/down (navigation cube top/bottom), where the
			// cross product collapses. Matches the camera side axis in Scene.
			Vector3 right = MakeVector3(-sinf(yaw), cosf(yaw), 0.0F);

			float step = speed * (keySprint ? kSprintMultiplier : 1.0F) * dt;
			bool descending = keyDown && DescendKeyIsActive(now);
			auto displacement = [&](bool withDescend) {
				Vector3 move = MakeVector3(0, 0, 0);
				if (keyFwd) move += fwd;
				if (keyBack) move -= fwd;
				if (keyRight) move += right;
				if (keyLeft) move -= right;
				if (keyUp) move += up;
				if (withDescend && descending) move -= up;
				if (move.x == 0.0F && move.y == 0.0F && move.z == 0.0F)
					return MakeVector3(0, 0, 0);
				return move.Normalize() * step;
			};

			Vector3 delta = displacement(true);
			// Moving the orbited point carries the whole view along with it, and
			// takes over from a flight.
			if (delta.x != 0.0F || delta.y != 0.0F || delta.z != 0.0F)
				flying = false;
			target += delta;

			// While Ctrl is both the descend key and held, remember how far the
			// descend part alone moved us, so a Ctrl shortcut can take it back.
			if (descending && ctrlHeld && DescendIsControl())
				ctrlDescent += delta - displacement(false);
		}

		client::SceneDefinition EditorCamera::Scene(float width, float height) {
			client::SceneDefinition sceneDef;
			Vector3 eye = target - Forward() * distance;
			Vector3 up = MakeVector3(0.0F, 0.0F, -1.0F);

			Vector3 dir = (target - eye).Normalize();
			// At the navigation cube Top/Bottom views dir is parallel to the
			// world-up reference, so Cross(dir, up) collapses to NaN. For every
			// non-vertical pitch Cross(dir, up).Normalize() equals (-sin(yaw),
			// cos(yaw), 0); use that heading-derived value at the pole too. It is
			// the continuous limit of the cross product, so the camera reaches
			// top/bottom smoothly instead of snapping, and only flips if rotated
			// past the pole.
			Vector3 side;
			if (std::fabs(Vector3::Dot(dir, up)) > 0.999F)
				side = MakeVector3(-sinf(yaw), cosf(yaw), 0.0F);
			else
				side = Vector3::Cross(dir, up).Normalize();
			up = -Vector3::Cross(dir, side);

			sceneDef.viewOrigin = eye;
			sceneDef.viewAxis[0] = side;
			sceneDef.viewAxis[1] = up;
			sceneDef.viewAxis[2] = dir;
			sceneDef.fovY = DEG2RAD(kVerticalFovDegrees);
			sceneDef.fovX = 2.0F * atanf(tanf(sceneDef.fovY * 0.5F) * (width / height));
			sceneDef.zNear = kNearPlane;
			sceneDef.zFar = ViewDistance();
			sceneDef.viewportLeft = 0;
			sceneDef.viewportTop = 0;
			sceneDef.viewportWidth = int(width);
			sceneDef.viewportHeight = int(height);

			view.eye = sceneDef.viewOrigin;
			view.right = sceneDef.viewAxis[0];
			view.up = sceneDef.viewAxis[1];
			view.forward = sceneDef.viewAxis[2];
			view.tanHalfFovX = tanf(sceneDef.fovX * 0.5F);
			view.tanHalfFovY = tanf(sceneDef.fovY * 0.5F);
			view.viewportX = 0.0F;
			view.viewportY = 0.0F;
			view.viewportWidth = width;
			view.viewportHeight = height;
			return sceneDef;
		}
	} // namespace gui
} // namespace spades
