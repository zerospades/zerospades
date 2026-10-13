/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <array>

#include "Flashlight.h"
#include "FlashlightGlare.h"
#include "Player.h"
#include <Core/Math.h>
#include <Core/RefCountedObject.h>
#include <ScriptBindings/ScriptManager.h>

namespace spades {
	class ScriptFunction;

	namespace client {
		class Client;
		class IRenderer;
		class IAudioDevice;
		class SandboxedRenderer;

		// TODO: Use `shared_ptr` instead of `RefCountedObject`
		/** Representation of player which is used by
		 * drawing/view layer of game client. */
		class ClientPlayer : public RefCountedObject {
			Client& client;
			Player& player;
			/** The audio tag of this player's reload sounds. */
			int reloadSoundTag;

			float sprintState;
			float aimDownState;
			float toolRaiseState;
			Player::ToolType currentTool;
			float time;
			bool wasAimingDownSight;

			/**
			 * Indicates whether the third-person weapon skin script has the latest
			 * value of `OriginMatrix`.
			 */
			bool hasValidOriginMatrix;

			Vector3 viewWeaponOffset;
			Vector3 lastFront;
			Vector3 classicViewWeaponOrigin;

			/** This player's lamp as the camera sees it, this frame. */
			FlashlightGlare flashlightGlare;

			/** The dropouts of this player's beam, if its Light Config flickers. */
			FlashlightFlicker flashlightFlicker;

			/** Whether this player's beam is lit right now: switched on, giving light,
			 * and not in a flicker's dropout. */
			bool IsFlashlightLit();

			asIScriptObject* spadeSkin;
			asIScriptObject* blockSkin;
			asIScriptObject* weaponSkin;
			asIScriptObject* grenadeSkin;

			asIScriptObject* spadeViewSkin;
			asIScriptObject* blockViewSkin;
			asIScriptObject* weaponViewSkin;
			asIScriptObject* grenadeViewSkin;

			asIScriptObject* GetCurrentSkin(bool viewSkin);

			Handle<SandboxedRenderer> sandboxedRenderer;

			/** Where this player's flashlight points. */
			Vector3 GetFlashlightDirection();
			std::array<Vector3, 3> GetFlashlightAxes();

			/**
			 * Get the base path of the player model set to use for this player.
			 * Zombies use a dedicated model set, everyone else uses the regular one.
			 */
			std::string GetModelPath();

			/**
			 * Whether a lamp at `lightOrigin` sits inside a solid voxel, in which case
			 * it would light the far side of that block: the map only hides a light
			 * from the blocks between it and its own. Only meaningful for a lamp the
			 * camera isn't standing at.
			 */
			bool IsLampBuried(const Vector3& lightOrigin);

			/** Where the glare shows: on the front of the headlamp worn by `head`. */
			Vector3 GetHeadlampLens(const Matrix4& head);

			/** The headlamp model's bounds, in its own voxels. */
			AABB3 GetHeadlampBounds();

			/**
			 * Offer this player's flashlight, from `lightOrigin`, to the client's
			 * `FlashlightBudget`, if it should be lit at all; `viewed` when the camera
			 * looks through this player's eyes. Shared by the first- and third-person
			 * paths, which differ only in where the lamp sits.
			 */
			void AddFlashlightToScene(const Vector3& lightOrigin, bool viewed);

			/** How far this player's lamp has come up since it was switched on, from
			 * 0 to 1. */
			float GetFlashlightFadeIn();

			/**
			 * Work out this frame's glare of this player's lamp, which sits at
			 * `lampPosition`. Only for a lamp the camera isn't standing at.
			 */
			void UpdateFlashlightGlare(const Vector3& lampPosition);

			void AddToSceneThirdPersonView();
			void AddToSceneFirstPersonView();

			void SetSkinParameterForTool(Player::ToolType, asIScriptObject*);
			void SetCommonSkinParameter(asIScriptObject*);

			struct AmbienceInfo;
			AmbienceInfo ComputeAmbience();

			// TODO: Naming convention violation
			asIScriptObject* initScriptFactory(ScriptFunction& creator,
				IRenderer& renderer, IAudioDevice& audio);

		protected:
			~ClientPlayer();

		public:
			ClientPlayer(Player& p, Client&);
			Player& GetPlayer() const { return player; }

			void Update(float dt);
			void AddToScene();
			void Draw2D();

			/** How strongly the glare of this player's lamp worked out by `AddToScene`
			 * shows (see `FlashlightGlare::GetStrength`). */
			float GetFlashlightGlareStrength() const { return flashlightGlare.GetStrength(); }

			/** Adds the glare of this player's lamp worked out by `AddToScene` to the
			 * scene, in a scene as bright as `ambient`, keeping `share` of it (see
			 * `FlashlightGlare::AddToScene`). */
			void AddFlashlightGlareToScene(float ambient, float share);

			bool IsChangingTool();
			void FiredWeapon();
			void EjectedBrass();
			void ReloadingWeapon();
			void ReloadedWeapon();
			/** Tells the skin how far through its animation the reload was, then
			 * stops the sounds the skin started for it unless the skin keeps them. */
			void ReloadAborted(float progress);

			float GetSprintState() { return sprintState; }

			/** 0 = normal, 1 = aiming down the sight. */
			float GetAimDownState() { return aimDownState; }

			bool ShouldRenderInThirdPersonView();

			/**
			 * Get the world coordinates representing the position of the currently wielded weapon's
			 * muzzle.
			 */
			Vector3 GetMuzzlePosition();

			/**
			 * Get the world coordinates representing the position of the currently wielded weapon's
			 * muzzle, based on the player's first-person view rendering.
			 */
			Vector3 GetMuzzlePositionInFirstPersonView();

			/**
			 * Get the world coordinates representing the position of the currently wielded weapon's
			 * case ejection port.
			 */
			Vector3 GetCaseEjectPosition();

			/**
			 * Get the world coordinates representing the position of the currently wielded weapon's
			 * case ejection port, based on the player's first-person view rendering.
			 */
			Vector3 GetCaseEjectPositionInFirstPersonView();

			Matrix4 GetEyeMatrix();
		};
	} // namespace client
} // namespace spades